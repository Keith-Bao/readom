# 交接前的静止顺序与 eMMC 访问路线（源码审计，2026-11-19）

依据：原厂 4.1.15 源码（`linux-headless-smoke` 树，由原厂树派生、禁用部分 EINK/Falcon 路径的实验树）、同一 GPL 包里的
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

以下构建问题已在实验树处理（不是当前待办）：`kernel/printk/printk.c` 的 `dump_lastk_to_mmc()` 无条件引用
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

## 四、当前代码采用的静止顺序（硬件实现待完成）

早期草案先下线次核、再关中断停设备；当前以 boot/koa2/quiesce.h 的顺序为准。
这张顺序表已有主机回滚测试，尚未接入设备模块；2026-11-21 的暂存测试没有执行下列硬件步骤。

用户空间先 sync，并安排文件系统卸载或只读重挂。之后依次：
校验计划和复制表 → EPDC/ePXP → Wi-Fi → USB gadget → SDMA → eMMC 静止 →
原厂 cpu_down(1) → 屏蔽 IRQ/FIQ → 喂一次看门狗 → 清理并关闭缓存。

关闭缓存为不可逆点。之前的每一步都需要 run/undo；失败按逆序回滚，undo 失败也须记录。
硬件实现须明确每步的完成条件、超时和恢复方式，不能把顺序测试当作硬件已验证。
跳转蹦床仅在这之后关 MMU、按复制表复制载荷并跳转；它尚未编进设备模块。

**不得**经由 kernel_restart() / machine_restart()，否则会进入原厂重启路径并写 printk 到 eMMC。
EPDC 动态帧缓冲须在当次交接采集并加入排除；下节地址只是此前一次启动的观察值。

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

## 六、静止由谁来做（2026-11-21 查明，改了原本的设计）

原本打算让交接模块自己停设备。做不到，也不该这么做，两个原因都是硬的：

**一、能停设备的符号基本都核对不了。** 原厂内核开了 modversions，模块的每个导入都要
CRC 对得上才能加载。拿设备上 63 个原厂模块核对（见 [stock-module-abi.md](stock-module-abi.md)）：
`clk_disable_unprepare`、`dmaengine_terminate_all`、`mmc_suspend_host`、`usb_gadget_disconnect`、
`device_shutdown`、`local_irq_disable` 这些构建树根本不导出；`dpm_suspend_start`、
`device_release_driver`、`fb_set_suspend` 导出但没有任何原厂模块用过，CRC 无从核对；
`lock_fb_info` 的 CRC **与原厂不一致**——模块一碰 framebuffer 就会被拒绝加载。

**二、停下来的设备，寄存器读不得。** EPDC 驱动每次碰寄存器之前都先
`clk_prepare_enable`，卸载时 `clk_disable_unprepare`。i.MX7 上时钟被 gate 之后读外设寄存器
不是返回旧值，而是挂总线。所以"读 DMA 引擎状态寄存器确认它空了"这种检查，
**恰恰会在静止成功的那一刻把设备挂死**。PW3 的 engine-guard 能那么写，是因为它在
设备还活着的时候读；这里不行。

所以改成：**停设备交给用户空间，走内核自己的驱动卸载和 CPU 下线路径**；
模块只检查结果，而且只用原厂模块证实过 CRC 的符号。

| 主设备 | 怎么停 | 停了之后 |
|---|---|---|
| EPDC + ePXP | `rmmod mxc_epdc_v2_fb` | 它的 remove 路径会等当前刷新完成、关面板电源、**释放 DMA 帧缓冲**。所以"交接当次要读出 EPDC 帧缓冲地址"这件事不必做了：那几段内存变回普通空闲页 |
| Wi-Fi（usdhc1） | `rmmod bcmdhd` | SDIO 功能停止 |
| USB gadget | `rmmod g_mass_storage` 等 | 断开 gadget、停控制器 |
| 触摸、传感器 | `rmmod cyttsp5_i2c` 等 | 不再取中断 |
| SDMA、APBH、PXP | 不能卸载（`=y` 编进内核） | 它们是被动引擎：只有客户请求时才搬数据，而客户就是上面那些驱动。停掉客户就没有流量——这是论证，不是测量 |
| usdhc3（eMMC） | **停不了** | 它是根文件系统，用户空间自己跑在上面。只能 sync 并只读重挂，然后让之后的窗口尽量短 |
| 次核 | `echo 0 > /sys/devices/system/cpu/cpu1/online` | 走的就是原厂 `cpu_down`，但不用导入它（它没有原厂 CRC，核对不了） |

模块这一侧是 `boot/koa2/quiesce-verify.c`：一个核在线、四个驱动都不在、根文件系统只读、
看门狗喂过不超过 10 秒。它**不读任何外设寄存器**。这些证据比读引擎状态弱，这一点写在
代码注释里，不含糊。

2026-11-23 真机结果：EPDC 驱动仍被 3 个引用占用，触摸模块虽重新加载却没有恢复触摸，USB 存储重载报 `Invalid argument`。触摸在强制重启后恢复。旧脚本还把原本只读的根文件系统改成了可写。

`ports/koa2/stage-test/quiesce-dryrun.sh` 现为只读预检，记录驱动、输入设备、USB 配置、根文件系统和 CPU 状态。`koa2-quiesce-dryrun` 在模拟设备上验证采集不改动这些状态。新一轮停用测试需要先验证实际触摸事件和 USB 存储恢复，并确认 EPDC 有可用的停用路径。

### 原厂恢复路径审计

- **USB**：原厂 `g_mass_storage` 调用 `fsg_config_from_params()` 生成 LUN。无显式 `luns` 时，它以 `file_count` 或 1 为默认数目。若 LUN 既没有后备介质也未标为可移除，`fsg_common_create_lun()` 返回 `-EINVAL`。这与旧空跑的无参重载报错一致。原厂日志中的两条空介质记录后面各有 `Number of LUNs=1`。正常重启后的两次只读预检读到 `luns=0`、`file` 为空、`removable=Y`，其中 `luns=0` 是自动选择，并非没有 LUN。历史日志显示 UDC 上线后原厂软件才把 `/dev/loop/0` 交给 LUN，断开后又清空。模块加载参数和插线后的介质切换是两步。待从上一次启动日志确认旧空跑的具体 errno 来源。恢复验收包括电脑重新读到 Kindle 盘。
- **触摸**：原厂驱动的 I²C probe 会调用 `cyttsp5_probe()`；正常启动日志先有 HID 超时，重试后才注册 `cyttsp5_mt`。模块重新加载只是恢复的一步。新增被动探针检查 `cyttsp5_mt` 的接触事件和对应的 `SYN_REPORT`，忽略丢包后的半帧，报告不含坐标。正常系统上已两次收到完整接触帧，I²C 绑定均为 `1-0024`。仍需查明卸载后重新绑定是否成功，以及原厂输入服务如何重连。
- **整机空跑**：保持暂停。EPDC 仍被占用，恢复流程尚未证实。后续逐个外设独立验证，保留测试前的根文件系统模式。

#### 上一次启动日志回收

2026-11-23 的只读采集读到 688 行有时间戳的记录，筛出 8 项相关事件。触摸只记录了启动初期的 HID 描述符读取失败、进入工作模式及输入设备注册。USB 记录了启动时 gadget 就绪及两次介质装入和清空。采集未找回旧空跑对应的 LUN 拒绝信息或后续触摸注册，无法只凭这些日志确定回滚失败发生在驱动 probe 还是原厂输入服务。

后续独立验证 USB 时，先保留在无介质状态下加载的可移除 LUN 参数，由原厂 USB 流程处理正常插拔和介质切换。恢复验收须包含电脑读取设备内容。不要用固定路径手动挂接后备介质。触摸恢复验证分三项：I²C 重新绑定、`cyttsp5_mt` 给出完整接触帧、原厂界面收到触摸。现有只读探针验证了重启后的前两项，实际卸载回滚仍未通过。

#### USB 单项验证

`koa2-usb-lun` 从校验过的原厂源码包编译执行参数映射和介质检查。无参数时得到一个没有后备介质、不可移除的 LUN，介质检查返回 `-EINVAL`。用预检观察到的可移除属性后，同一个空 LUN 通过这项检查。这个主机用例覆盖模块参数到 LUN 的转换及空介质拒绝条件，实际控制器绑定和原厂 USB 服务接管仍待验证。

在正常系统上，先把任一次只读预检的 `report.txt` 保存到电脑，保留其运行目录名。插线后可从 Windows 运行 `powershell -File ports/koa2/check-usb-reconnect.ps1 -ReferenceReport <本地报告>`。它自动查找 Kindle 的盘符，实际读取设备上的原报告并与电脑备份核对 SHA-256。单项恢复实验须分别记录试验前和恢复后的结果；后者还须保留内核和 UDC 日志，避免把重启后的可读状态算成原地恢复成功。

2026-11-23 USB 单项真机测试记录了 `usb_module_removed=1` 和 `usb_idle_rebound_host_check_pending`。重接后 Windows 读回 `D:\` 上的预检报告，SHA-256 与本地备份一致。USB 单项成立，整机空跑仍暂停。

#### 触摸读者基线

现在 `Check live touch (tap screen)` 在记录接触帧后，也检查 `/proc/<pid>/fd` 是否仍指向当前触摸事件节点，按进程记录 `comm`。它只检查此节点，其他打开的文件、进程参数和触摸坐标均不采集。若没有观察到打开者，单次快照不足以判断原厂界面没有读者。确认原厂进程如何发现或重新打开事件节点前，暂缓触摸驱动重载。

2026-11-23 真机报告在 `1-0024` 绑定的 `cyttsp5_mt` 上收到完整接触事件，`Xorg` 是当时唯一观察到的读者。固件 5.16.2.1.1 的 GPL 发布包内有 `xf86-input-evdev_2.4.0` 和 `xserver_1.8.2`。前者在读取返回 `ENODEV` 后关闭旧句柄，重新打开由设备启用路径处理。后者的 udev 后端可以根据设备的 add/remove 事件增删输入，add 还要求 `ID_INPUT`。这些是源码支持的路径，仍需从实机确认触摸节点的添加来源、驱动和 udev 属性。

KUAL 的 **Inspect Xorg touch configuration** 只读检查这三项。报告记录匹配的输入模块名、Xorg 日志分类和两个输入属性。此步骤无需触摸事件采样，不重启 Xorg 或重载驱动。

2026-11-24 真机检查确认：Xorg 打开了 `/dev/input/event9`（`cyttsp5_mt`，I²C `1-0024`），加载了 `evdev_drv.so`，日志显示 `Automatically adding devices`。这份报告未识别出触摸设备的添加来源，也未读到相应的 udev 输入属性。现有证据支持调查热插拔路径，尚未证明 Xorg 会重新打开重载后出现的触摸节点。

保留输入节点是另一条待查路径。发布源码提供触摸驱动的 IRQ 控制和 suspend/resume 回调，但驱动还有独立的 watchdog 工作队列。单独停 IRQ 不足以证明 I²C 已静止，不能用这个状态替代交接校验。下一步在主机端审计原厂休眠是否能停止相关工作并保留输入节点，再决定如何验证触摸恢复。

#### 原厂休眠路径审计（2026-11-24）

校验源码包后核对了 Zelda 设备树和 CYTTSP5 驱动。`tsc@24` 的 `cy,flags = <6>` 启用了 `POWEROFF_ON_SLEEP`，`easy_wakeup_gesture` 为 `0`。`cyttsp5_core_sleep_()` 同步取消 watchdog 工作及 startup 工作，再走触摸控制器断电路径。唤醒恢复供电、IRQ 和控制器初始化。触摸的 framebuffer 休眠/唤醒回调只暂停报告并调整运行时电源引用；休眠路径不注销输入设备，所以源码允许 Xorg 保留现有句柄。

完成条件仍需独立验证。断电路径用 `disable_irq_nosync()`，没有等待已在运行的 IRQ 处理完成。断电返回错误时，驱动也写入 `SS_SLEEP_ON`，因此 `sleep_status=off` 无法证明断电成功。运行时休眠能返回 `-EAGAIN`，整机 suspend 回调却忽略了触摸休眠的返回值。现有交接校验继续要求满足原条件。

接下来先观测一次原厂正常休眠和唤醒。比较同一次开机内触摸节点的 inode、Xorg 进程和打开的句柄，并在醒来后确认接触事件与原厂界面响应。结合 PM 日志和计数器区分实际休眠和单纯息屏。这项验证可以回答节点是否保留、界面是否恢复；I²C 停止活动仍需要独立证据。暂不写触摸调试接口，也不再卸载触摸驱动。

2026-11-24 的首次观测返回 `stock_sleep_not_observed`。两次快照的开机标识、`event9` inode 和 Xorg 进程/句柄标记均一致，系统累计休眠时间却没有增加。当前脚本在此结果下未运行接触事件探针。这个结果仅说明该次锁屏没有观测到系统休眠，尚不能评价真正休眠后的触摸恢复。先查原厂电源管理何时进入 suspend，以及能否读取挂起成功计数，再安排下一批观测；暂停重复相同的两分钟流程。

同日新版观测返回 `stock_sleep_touch_observed`。同次开机内挂起成功计数从 0 增至 2，失败计数保持为 0，累计休眠增量为 3,602,010 ms。`event9` inode 和 Xorg 读者标记在两个快照中相同，醒后探针收到完整接触事件。报告已在电脑上校验备份，测试入口和报告副本已从 Kindle 清理。原厂休眠唤醒的触摸内核侧通过；交接阶段 I²C 静止仍待独立验证。下一批先在主机审计可逆触摸静止和 EPDC 的占用来源。

#### 触摸单项静止设计（主机侧，2026-11-24）

源码结论：`cyttsp5_core_sleep_()` 先把状态设为 `SS_SLEEPING`，同步停 watchdog 定时器和工作（`del_timer_sync` + `cancel_work_sync`），再 `cancel_work_sync(startup_work)`，然后关 `touch_1v8` GPIO 和 `MAX77796-LSW1`。watchdog 工作在 `SS_SLEEPING` 下直接返回。中断用 `disable_irq_nosync()` 关，不等在途线程处理结束，所以“已停”要靠事后观测。`drv_debug` 写 `4`/`5` 直接调用原厂休眠和唤醒，不卸载驱动，输入节点和 Xorg 句柄不变；写 `5` 时 `wd_disabled` 清零并重启 watchdog。屏幕亮灭会经 framebuffer 回调和运行时电源引用再次调用休眠或唤醒，脚本检测到即作废结果。

独立证据：i2c2（`30a30000.i2c`）上只有触摸控制器，i.MX I²C 驱动按字节中断，所以它的中断计数代表触摸 I²C 流量。醒着时 watchdog 每 2 秒发一次空命令，计数必须增加，这是对照。休眠后 5 秒窗口内须满足：i2c2 和触摸中断都为 0，调节器 `disabled`，供电 GPIO `out lo`，中断线程 `S`，日志恰好一次 `Suspend succeeded`。

`ports/koa2/stage-test/touch-quiesce.sh` 实现上述流程，失败和中断都会尝试一次唤醒，并核对节点 inode、Xorg 读者和醒后接触帧。`koa2-touch-quiesce` 用模拟驱动覆盖 16 种情形，另做了 5 个故意改坏的版本，测试全部能发现。i2c2 只有触摸、按字节中断这两点来自源码和设备树，需要真机报告的醒着计数佐证。这项结果不放宽 `quiesce-verify.c`；交接时是否改为“休眠而不卸载”，要等真机通过后再定。
