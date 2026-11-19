# 最小 smoke initramfs（主机侧，2026-11-19）

目的：让实验内核启动后能被观察到，并且**自动回到原厂**，而不是停在一台没有显示、
没有串口引出的机器上。还没有在设备上运行过。

## 它做什么

`smoke-init.c` 是 PID 1，顺序固定：

1. 挂 proc、sysfs（只读）、devtmpfs。**不挂任何存储，不打开任何块设备。**
2. 把启动标记写到 stdout 和 `/dev/kmsg`：内核 release、在线 CPU、MemTotal，以及
   `/sys/block` 里的设备**名字**（证明标准 eMMC 驱动绑上了，但不读写设备）。
3. 打开 `/dev/watchdog`，按 `WDIOC_SETTIMEOUT` 设定超时并回读。
4. 在 `koa2_smoke_hold` 秒内每秒喂一次狗，每 5 秒写一行 `alive t=<秒>`。
5. 写 `PASS released_watchdog expect_reset_within=<秒>`，然后**停止喂狗且不关闭 fd**，
   等看门狗硬件复位，回到原厂引导。
6. 从不调用 `reboot()`，所以不经过 `imx2_restart_handler`（见 handoff-quiesce.md 第一节）。

两个参数来自内核命令行：`koa2_smoke_hold=`（1–50 秒，默认 20）、`koa2_smoke_wdt=`（2–60 秒，默认 30）。

**为什么必须打开 `/dev/watchdog`**：`imx2_wdt` 在 `CONFIG_LAB126` 下 probe 时就启动看门狗，
并用自己的内核定时器每 `timeout/2` 秒喂一次，直到 `/dev/watchdog` 被打开（`imx2_wdt_start`
会 `del_timer_sync`）。所以内核活着但用户空间卡住时**不会**自动复位；只有内核自己停转才会。
`nowayout=0` 且没有写入魔数 'V' 就关闭 fd 会把喂狗交回内核定时器，因此 init 故意不关闭它。

## 构建

```bash
bash ports/koa2/build-smoke-initramfs.sh
```

产物在 `~/artifacts/smoke-initramfs/`：静态 ARM `init`、`initramfs.cpio.gz`、`SHA256SUMS`。
`init` 用 `arm-linux-gnueabi-gcc -static` 编（与内核工具链不同，这是用户空间程序）。

## 主机测试

命令行解析拆到 `smoke-params.h`，由 `test-smoke-params.c` 覆盖 17 种输入（缺省、
越界、非数字、`xkoa2_smoke_hold=` 这类后缀误匹配、重复出现、边界值）。
登记在 `tools/run-tests.py` 的 `koa2-smoke-params`，ASan/UBSan 下通过，
同一份源码的 ARM 静态版在 `qemu-arm` 下也通过。

## QEMU 整机启动：做不到，原因已查明

试过用 `qemu-system-arm -M mcimx7d-sabre` 跑这个内核加 initramfs（`run-qemu-smoke.py`）。
**失败，不是内核的问题，而是 QEMU 6.2 没有模拟这块 SoC 的几处片上资源**，每绕过一个就撞上下一个：

| 停在哪里 | 原因 |
|---|---|
| `imx7_pm_map_io` → `__memzero` 外部中止 `0x0018000c` | QEMU 没有 OCRAM_S（`fsl,lpm-sram`） |
| 同上，`0x00900000` | 没有 DDR 低功耗 OCRAM（`fsl,ddr-lpm-sram`） |
| `imx_boot_secondary` 死等 | 没有 GPCv2 的 core-1 上电握手（加 `maxcpus=1` 可绕过） |
| `imx_ddrc_probe` 外部中止 `0x307a0000` | 没有 DDR 控制器 |
| `imx7_pm_common_init` Oops | 又要用前面被去掉的 OCRAM：**循环依赖，到此为止** |

结论：这套内核带着原厂的低功耗与 DDR 代码，必须有真实 i.MX7D 的片上内存才能启动；
QEMU 的 i.MX7D 模型不足以跑它。`run-qemu-smoke.py` 留在仓库里，记录做到哪一步，
但它现在不是通过的测试，也不列入 `run-tests.py`。

真正的启动验证只能在设备上做，而设备实验的前提仍未满足（交接模块只有校验、没有跳转）。

## 设备上要怎么用（尚未执行）

1. 由用户发起，机器插着电、电量充足。
2. 交接模块先跑完 `boot/koa2/contract.c` 的校验；任一项不过就退出，不改状态。
3. 跳转进实验内核，命令行带 `rdinit=/init`，initramfs 为上面这份。
4. 预期：约 `hold + wdt` 秒后机器自己复位回原厂。开机后在原厂系统读
   `/proc/printkbuf`，应能看到 `KOA2_SMOKE` 那几行。
5. 如果读不到任何 `KOA2_SMOKE`，说明没进到用户空间；这时唯一的信息来源仍是
   printk 备份区，而它是否跨 WDOG_B 复位保留，**还没有验证过**（见 boot-preflight-findings.md）。
