# 交接前的静止顺序与 eMMC 访问路线（源码审计，2026-11-19）

依据：原厂 4.1.15 源码（`linux-headless-smoke` 树，与原厂只差版本标识）、同一 GPL 包里的
`uboot-4.1.15`、用户运行的只读 Boot Preflight（见 boot-preflight-findings.md）。
**全部是源码与日志层面的结论，没有在设备上执行过任何一步。**

## 一、eMMC 能否不经 Falcon BIOS

| 事实 | 出处 |
|---|---|
| mmcblk2 在 usdhc3 `0x30b60000`，8 位总线，non-removable | 运行时 DTB |
| 芯片 dtsi 给 usdhc3 的是 `"fsl,imx7d-usdhc", "fsl,imx6sx-usdhc"` | `arch/arm/boot/dts/imx7d.dtsi:1316` |
| zelda 板级把它改成 `compatible = "falcon_blk"` | `imx7d-zelda.dts:677` |
| `sdhci-esdhc-imx` 支持 `fsl,imx7d-usdhc`（USDHC、STD_TUNING、HS400 标志） | `drivers/mmc/host/sdhci-esdhc-imx.c:196,245` |
| 运行时节点没有 HS200/HS400/vqmmc 属性；pinctrl 有 100/200 MHz 两组 | 运行时 DTB |
| Falcon BIOS 本身来自 eMMC 的 `sbios`/`fbios`，由 U-Boot 放到 0x9ffc0000 | `uboot-4.1.15/include/configs/mx7d_zelda.h:467-476` |
| 原厂运行中加载闭源模块 `falcon`（newBSD），它申请约 4.4 MiB 工作区 | 运行时日志 7.70 s |

结论：**源码上可行**——实验内核的 FDT 把 usdhc3 的 compatible 改回芯片 dtsi 的标准值，
就由标准 sdhci 驱动访问 eMMC，不再调用 Falcon BIOS，也就不再依赖 0x9ffc0000 那片代码与
闭源 `falcon` 模块。

要先解决一处构建问题：`kernel/printk/printk.c` 的 `dump_lastk_to_mmc()` 无条件引用
`__dump_lastk_to_mmc()`，而后者只在 `CONFIG_FALCON_BLK` 的 `drivers/falcon/block.c` 里。直接关掉
`FALCON_BLK` 会链接失败。实验内核需要一个只在实验版本标识下生效的补丁，让 `dump_lastk_to_mmc()`
成为空操作——这也同时去掉了新内核重启和长按电源键时写 eMMC 原始扇区的行为。

未核实（需要上机）：标准驱动在这块 eMMC 上能跑到的速度模式、是否需要 tuning，以及
`keep-power-in-suspend` 下的休眠唤醒。

## 二、看门狗

- 原厂日志：`imx2-wdt 30280000.wdog: timeout 60 sec`、`use WDOG_B to reboot`、
  `kernel wdt is enabled and running`。运行时 DTB 的 wdog1 节点带 `fsl,wdog_b`、没有 status（即启用）。
- i.MX 看门狗的 WDE 位一旦置位不能清除，所以**交接后看门狗仍在计时**，新内核必须在 60 秒内接管喂狗；
  4.1 的 `imx2_wdt` 在 probe 时检测到已在运行就会自己定时喂。
- 这对实验是好事：新内核卡死时，最迟约 60 秒硬件复位（WDOG_B → PMIC），回到原厂启动。
  硬件超时复位不经过 `imx2_restart_handler`，不会调用 `dump_lastk_to_mmc`。
- 约束：交接从停喂到新内核 probe 看门狗的时间必须明显短于 60 秒；最小 initramfs 不能拖延 probe。

## 三、次核

- 原厂 2 核都在线；`cpu_down`、`cpu_up`、`imx_cpu_die`、`imx_cpu_kill`、
  `imx_gpcv2_set_core1_pdn_pup_by_software` 均在 kallsyms 中（`cpu_down` 是导出符号）。
- `imx_cpu_die()`：次核进入低功耗、写 SRC 跳转参数寄存器后 WFI；`imx_cpu_kill()`：主核等这个信号，
  清 SRC_A7RCR1 的 core1 使能，再经 GPCv2 软件断电。新内核按常规路径 `imx_boot_secondary()` 重新上电。
- 所以交接模块应**调用原厂自己的 `cpu_down(1)`**，不自写次核停机代码；调用后检查
  `cpu_online(1)` 为假再继续。

## 四、建议的交接顺序（待实现，未验证）

1. 用户空间：同步、卸载或只读重挂原厂文件系统（PW3 的做法）。
2. 交接模块加载，**先只做校验**：运行时 FDT 结构、memreserve 全部被排除表覆盖、载荷位置合法
   （`boot/koa2/contract.c`）。任一失败即退出，不改任何状态。
3. `cpu_down(1)`，确认只剩 CPU0。
4. 关中断；停 SDMA、EPDC/PXP、USB 等 DMA 主设备（具体清单待逐个核对驱动）。
5. 喂一次看门狗，记下时间。
6. 复制载荷到已验证的目的地，清理/失效缓存，关 MMU，跳到新内核入口，传运行时 FDT
   （按需派生：bootargs、usdhc3 compatible）。
7. 新内核：先 probe 看门狗，再挂载最小 initramfs。

**不得**经由 `kernel_restart()` / `machine_restart()`，否则会进 `imx2_restart_handler` 写 eMMC 扇区
0x9a400（见 boot-preflight-findings.md 末节）。
