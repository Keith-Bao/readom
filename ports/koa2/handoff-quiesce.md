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
- i.MX 看门狗的 WDE 位一旦置位不能清除，所以**交接后看门狗仍在计时**。
- **更正（2026-11-19）**：`imx2_wdt` 在 `CONFIG_LAB126` 下 probe 时就 `imx2_wdt_start()` 并用
  自己的内核定时器每 `timeout/2` 秒喂狗，一直喂到 `/dev/watchdog` 被打开（`imx2_wdt_start`
  里 `del_timer_sync`）。所以"新内核卡死约 60 秒后自动复位"只在**内核本身停转**时成立；
  内核还在跑而用户空间卡住时，它会一直喂狗，不会复位。要让实验可自动结束，必须由用户空间
  占住 `/dev/watchdog` 再停止喂狗——smoke initramfs 就是这么做的（smoke-initramfs.md）。
- 硬件超时复位不经过 `imx2_restart_handler`，不会调用 `dump_lastk_to_mmc`。
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

## 五、需要静止的 DMA 主设备（运行时 DTB + 原厂启动日志，2026-11-19）

运行时 DTB 里启用、且原厂内核确实绑定了驱动的总线主设备：

| 设备 | 驱动/证据 | 交接前要做的 |
|---|---|---|
| EPDC `306f0000` + ePXP `30700000` | `imx_epdc_v2_fb`；帧缓冲**动态分配**在 0x99000000（4.4 MiB）、0x99800000、0x99c00000（各 2.2 MiB），工作缓冲 0x9c000000 | 等当前刷新完成（`MXCFB_WAIT_FOR_UPDATE_COMPLETE`），再停 EPDC 时钟。这三段不在排除表里：只要目的地仍限定在低端（见第四节），它们不会被覆盖；但 EPDC 未停时不能跳转 |
| SDMA `30bd0000` | `imx-sdma` 已加载固件 4.2 | 停所有通道；它服务 UART、ECSPI、SAI |
| APBH-DMA `33000000` | `mxs-dma` initialized | 目前没有 GPMI/NAND 使用者，确认无通道在用即可 |
| usdhc1 `30b40000` | `sdhci-esdhc-imx`，"assigned as wifi host"，SDIO 卡 | 先卸载/停 Wi‑Fi 驱动，再停控制器 ADMA |
| usdhc3 `30b60000` | `falcon_blk`（eMMC） | 用户空间先 sync 并卸载；交接时不能有正在进行的 BIOS 请求 |
| USB OTG `30b10000` | ChipIdea，peripheral 模式 | 断开 gadget，停控制器 |
| UART ×3、ECSPI ×2 | 经 SDMA | 随 SDMA 一起停 |

CAAM `30900000` 在 DTB 里没有 status（视为启用），原厂日志里没有它的 probe 记录，暂不列为活跃主设备；
上机时要再确认一次。

**新发现的约束**：EPDC 帧缓冲来自普通内存分配，地址每次启动可能不同。交接模块不能假定它们固定，
应在运行时读 `/proc/iomem` 或 EPDC 驱动的分配记录，并把它们加入当次的排除区；或者确保 EPDC 先停。
