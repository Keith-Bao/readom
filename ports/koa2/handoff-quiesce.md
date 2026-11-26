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

#### EPDC 引用来源（主机侧，2026-11-24）

源码结论：`fb_open()` 每次打开帧缓冲节点都对 `mxc_epdc_v2_fb` 调一次 `try_module_get`，`fb_release()` 归还（`fbmem.c:1458,1489`）。映射保留文件，因此关掉描述符后映射仍占引用。原厂配置没有 fbcon，不会有控制台占用。`request_firmware_nowait()` 也取一次引用，但设备日志显示 1.47 s 已进入 `mxc_epdc_fb_fw_handler`，工作完成后已归还。procfs 和 sysfs 文件不取模块引用。所以此前的 3 个引用最可能来自用户空间打开的 `/dev/fb0`，尚需实机确认。

KUAL **Inspect EPDC framebuffer users** 只读列出：模块引用数、`/sys/class/graphics/fb*` 名称、每个进程打开帧缓冲的描述符数和映射数，以及按进程计的 `comm`。不记录参数、地址或内容。`epdc_reference_accounting=matched` 表示观察到的打开者数等于引用数；`shared_files_possible` 表示 fork/dup 共享文件导致观察数更多；`more_references_than_openers` 表示还有源码未解释的引用。`koa2-quiesce-dryrun` 覆盖这四种结果，并确认采集不改动状态。释放这些引用之前，EPDC 停用仍暂停。

#### 2026-11-24 晚五项真机批次结果

使用 2026-11-24 套件在实机运行 5 项测试，原始报告与 `stock-runtime.dtb` 已校验备份到本地，设备上的测试入口与副本已清理：

- **EPDC 引用来源**：`epdc_module_users=3`、`epdc_open_fds=3`、`epdc_mapped_only_processes=0`、`epdc_reference_accounting=matched`。3 个打开者全部查明：`Xorg`（`fds=1 mappings=1`）、`awesome`（`fds=1 mappings=0`）、`blanket`（`fds=1 mappings=0`）。源码外不存在未知内核引用，后续可据此设计可控释放路径。
- **Wi-Fi（`bcmdhd`）**：采集时 `wifi_module_users=unloaded`，观察到 2 个关联进程：`lipc-daemon` 与 `wifid`（不含 MAC/SSID/密码）。
- **原厂运行时 DTB 备份**：`fdt_backup=ok`、`fdt_magic=d00dfeed`、`fdt_bytes=49280`、`fdt_sha256=c84e99f47b2b451cc777bf9a36bb6732c2501bca8952dc739118f2d1fd60cd4f`。本地留存，不提交 Git。
- **CPU1 单项下线与恢复**：`result=cpu1_quiesce_restored`。`online_before=0-1` → 离线窗口首尾均保持 `online=0`、`cpu1=0` → `online_after=0-1`，次核可逆下线/上线实机通过。
- **触摸单项休眠与唤醒**：实机返回 `refused_touch_not_awake`（安全拒绝，未写入 `drv_debug`），记录 `regulator=enabled`、`sleep_status=on`、`supply_gpio=out_lo`。核对原厂 `drivers/gpio/gpio-mxc.c:450`（`bgpio_init` 传入 `GPIO_PSR` 且未置 `BGPIOF_READ_OUTPUT_REG_SET`）与 `stock-runtime.dtb` 后确认：`touch_1v8`（`GPIO1_IO12`）未启用 `SION`，读输出引脚恒返 `out_lo`。`touch-quiesce.sh` 与 `koa2-touch-quiesce` 已修正为保留 `supply_gpio` 日志记录，但以 `sleep_status=on` + `MAX77796-LSW1=enabled` + 醒着时的 I²C 中断正对照作为前提，待下次上机复测。

- **触摸单项复测（修正 `supply_gpio` 前提后）**：实机完整跑完休眠与唤醒流程，报告已备份到本地并清理设备。醒着正对照 `awake_i2c_irqs=874`、`awake_touch_irqs=44`；写入 `4` 休眠后 `suspend_logged=1`、`suspend_failed_logged=0`、`asleep_status=off`、`asleep_regulator=disabled`、`asleep_irq_thread=S`、5 秒窗口内 **`asleep_i2c_irqs=0`**、`system_suspend_ms=0`、`screen_changes=0`；写入 `5` 唤醒后 `resume_logged=1`、`wake=ok`、`woken_i2c_irqs=502`、`after_touch=observed`，`event9` inode（`15:7148`）与 Xorg 句柄标记全程未变。脚本末行报 `touch_bus_not_quiet` 仅因 `asleep_touch_irqs=-2`——窗口期间原厂动态热插拔将 CPU1 下线，导致 `/proc/interrupts` 少了一列历史计数（差值为负而非新增中断）。结论：经由原厂 `drv_debug` 4/5 路径可逆停用并恢复触摸控制器（含 I²C2 总线归零与断电）已在实机证实。

#### 2026-11-24 主机侧跟进（运行时 DTB 审计、多核中断计数修正与 EPDC 可控停用设计）

- **原厂运行时 DTB 差异审计**：将本地备份的 `stock-runtime.dtb`（49,280 字节）与 `android41-kernel/imx7d-zelda.dtb` 标准化比对（跳过设备私有 `/idme`、`/chosen` 与 phandle 编号）。主要差异均为 U-Boot 按板级 `hwid=5` 注入或覆盖的真实硬件项：`/reserved-memory/epdc_reserved`（`0x9c000000`，5 MiB，`no-map`）与 `epdc@306f0000/reserved-working-buffer`、无蜂窝 `mwan` 节点、`bosch_accel@18` 的 `bma,place=1`、以及 `tsc@24` 的 `touch_sensor_defaultgrp`/`touch_sensor_sleepgrp`。由于交接流程由 `boot/koa2/fdt-rewrite.c` 直接在原厂运行时 DTB 上改写，上述板级差异在交接时会被自动保留。
- **触摸多核动态下线中断计数修正**：`touch-quiesce.sh` 的 `irq_line`/`irq_delta` 改为按 `/proc/interrupts` 表头 `CPU0`/`CPU1` 按列求差，CPU1 在 5 秒窗口内动态下线丢列时不再产生负差值；`koa2-touch-quiesce` 已新增 `cpu1_offline_mid_window` 用例。
- **EPDC 参数/服务只读探针与单项停用设计**：`quiesce-dryrun.sh epdc` 新增只读采集 `/sys/module/mxc_epdc_v2_fb/parameters/*` 与 Upstart 服务状态（`lab126_gui`/`x`/`awesome`/`blanket`/`framework`）。同时在主机侧落地 `ports/koa2/stage-test/epdc-quiesce.sh`（先校验内核与 SHA256、保存 `waveform` 参数、停 GUI 服务、确认 `mxc_epdc_v2_fb` 引用归零后 `rmmod`、按原参数 `insmod` 并重启 GUI；任意失败路径自动回滚）与 13 用例主机测试 `koa2-epdc-quiesce`。实机确认 `epdc_service_*` 与 `epdc_param_waveform` 之前，暂不加入 `menu.json`。

#### 2026-11-25 实机复测：触摸零中断与 EPDC Upstart 服务边界

- **触摸单项零中断确认**：使用修正按核中断计数后的 `touch-quiesce.sh` 实机复测，醒着正对照 `awake_i2c_irqs=51`、`awake_touch_irqs=3`；休眠窗口内 `asleep_regulator=disabled`、**`asleep_i2c_irqs=0`**、**`asleep_touch_irqs=0`**、`asleep_irq_thread=S`、`suspend_logged=1`、`resume_logged=1`、`wake=ok`。
- **EPDC 只读参数与服务采集**：新版 `quiesce-dryrun.sh epdc` 实机读回 `epdc_param_waveform=320_R198_AG3902_ED070KC2UA_TC.wrf.gz`，并确认三个独立运行的 Upstart 任务：`lab126_gui`、`x`（process 4379）和 `framework`（process 6586）。
- **EPDC 停 `lab126_gui` 实机观测**：首次尝试因 `initctl stop lab126_gui` 向同进程组发 `SIGTERM` 返回 `epdc_interrupted`；改用 `setsid` + `trap '' HUP TERM` 后完整执行并安全返回 `result=epdc_users_not_released`（`epdc_users_after_stop=3`，未触发 `rmmod`，已重新 `initctl start lab126_gui`）。这证实仅停 `lab126_gui` 只影响上层 `framework`（导致主页空白，正常重启即恢复），不会释放 `x` 进程树下的 `Xorg`/`awesome`/`blanket` 对 `/dev/fb0` 的 3 个引用。下一步需先只读审计 `/etc/upstart/x.conf` 及其依赖关系，再决定停 `x` 的安全顺序。

- **联合可逆空跑初测（`full-quiesce-dryrun.sh` v1，2026-11-25）**：实机触发卡死并由看门狗自动重启回原厂系统，因重启前未 `sync`，异步 FAT32 缓冲中的两份报告均未落盘。排查发现 v1 存在两处与已验证单项脚本不一致的问题：1）写入 `drv_debug=4` 后未等待 `sleep 1` 排空在途触摸中断即直接执行 `cpu1/online=0`；2）重载 USB 未沿用 `usb-reload.sh` 的 `removable=1` 形式，且各步骤间缺少 `sync`。已在主机侧修正 `full-quiesce-dryrun.sh` 并通过单测，Kindle 已清理，待后续先采集前次启动日志后再择机验证。
- **联合可逆空跑 v2（主机，2026-11-25）**：v1 卡死后无法判断停在哪一步，v2 在每步动作前写并 `sync` 一行 `stage=`，前提检查与恢复确认逐项沿用 `usb-reload.sh`、`touch-quiesce.sh`、`cpu1-quiesce.sh`（触摸以 `sleep_status` 加调节器状态判断，不单信 `sleep_status`）。另提供 `touch,cpu1` 与 `usb,touch` 两步组合，先排除 v1 的头号嫌疑：触摸休眠后紧接 CPU1 下线。尚未上机，见 [2026-11-25 操作单](materials/2026-11-25/README.md)。

- **离线审计原厂 OTA 根文件系统（主机，2026-11-25）**：从 Amazon 下载 Oasis 2 官方 OTA，用 KindleTool 解出 `rootfs.img`（63 个模块与真机采集字节一致）。确认 `x.conf` 是 `lxinit` 管理 Xorg/awesome/blanket 的入口（`start on x_should_start`，`stop on stopping dbus or stop_x`），`post-stop` 会 `killall -s KILL` 三者；Amazon 的 `dump-and-kill-cvm` 用 `stop x` / `start x` 做同样的事。`lab126_gui.conf` 只停 Java 框架（`stop on stopping x or ota-update`），不碰 X 进程树——这就是 2026-11-25 实机测试中 `initctl stop lab126_gui` 后 3 个引用不减的原因。`boot.img` initramfs 中的 `mxc_epdc_v2_fb.ko` 与 rootfs 副本功能相同（`.text`/`.data`/`.rodata`/`__versions`/`.modinfo` 全等，仅 `.symtab` 条目数差 1），`insmod` rootfs 副本安全。先将 `epdc-quiesce.sh` 改为 `initctl stop x` / `initctl start x`；2026-11-26 又据 `/etc/upstart/display`（`initctl emit --no-wait start_x`）与 `dump-and-kill-cvm`（直接 `start x`）确认开机后重启 X 的正道是**直接 `start x`**（`x_prelaunch` 的 `start_x and started dynconfig and started dbus` 复合条件开机后不再满足，单发 `emit start_x` 拉不起来），故 v3 保留 `start x` 但加 `--no-wait`，让 `initctl` 立即返回、成败交给 30 秒 fb0 重开轮询，并记录恢复后的 `x`/`lab126_gui` 状态。

#### 2026-11-25 晚实机重大突破：EPDC 驱动首次成功释放、卸载并原参数重载

使用 `2026-11-25` 14 文件套件实机运行 **EPDC only: stop X, unload and reload (reversible)**（`epdc-quiesce.sh` v2），原始报告已备份本地并清理设备：

- `users_before=3`、`mod_ko=/lib/modules/4.1.15-lab126/kernel/drivers/video/fbdev/mxc/mxc_epdc_v2_fb.ko`、`waveform=320_R198_AG3902_ED070KC2UA_TC.wrf.gz`、`x_status_before=x start/running, process 3078`。
- `stage=stop_x` → **`epdc_users_after_stop=0 wait=0`**（停 `x` 后 3 个 `/dev/fb0` 引用立即归零！）。
- `stage=rmmod` → **`epdc_unloaded=1`**（`mxc_epdc_v2_fb` 0.07 秒内成功卸载，最后一个 DMA 主设备在实机上证实可停！）。
- `stage=restore_insmod` → **`epdc_reloaded=1`**（按原 `waveform` 参数 0.21 秒内成功重载）。
- 末行停在 `stage=restore_x`（`initctl start x` 未返回，需重启恢复桌面；随后只读采集前次日志返回 `previous_boot_log_lines=38`、`previous_boot_result=log_filtered`）。对于真实交接路径（进入新内核无需原地重启 `x`），`stop x` → `users=0` → `rmmod mxc_epdc_v2_fb` 已在实机完全跑通。

- **2026-11-25 晚两组对照组合实机结果（定位上次联合空跑卡死根因）**：
  - **`Combined pair: Touch + CPU1 only`**：**全部通过（`result=full_quiesce_dryrun_restored`）**。`touch_power_before=on_enabled` → `touch_power_asleep=off_disabled` → `step_cpu1=offline`（`cpus=0`）→ `restore_cpu1=ok` → `restore_touch=ok`（`touch_power_restored=on_enabled`），总耗时 3.5 秒，彻底排除了触摸休眠与 CPU1 下线冲突的嫌疑。
  - **`Combined pair: USB + Touch only`**：在 **`stage=usb_unload uptime=626.48 cpus=0-1`**（执行 `rmmod g_mass_storage` 时，尚未进入触摸步骤）卡死并触发看门狗重启；随后采集前次日志返回 `previous_boot_result=buffer_unreadable`。由此彻底锁定此前联合空跑卡死的直接触发点就是 `rmmod g_mass_storage`（需在主机侧对照原厂 `ci_hdrc` / `g_mass_storage` 源码审计为何 UDC 低功耗/拔线状态下 `rmmod` 会挂死）。原始报告已归档本地，Kindle 已清理。

#### 2026-11-27 实机结果（2026-11-26 v3 套件）：EPDC 完整恢复通过，确认 `udc_state=0` 下 `soft_connect` 同样挂死

使用 `2026-11-26` v3 套件在实机运行，原始报告已归档本地并清理 Kindle：

- **EPDC 单项（`epdc-quiesce.sh` v3）**：**`result=epdc_quiesce_restored`**。`stage=stop_x` → `epdc_users_after_stop=0 wait=0` → `stage=rmmod`（`epdc_unloaded=1`）→ `stage=restore_insmod`（`epdc_reloaded=1`）→ `stage=restore_x`（`x_started=1`、`x_status_after=x start/running`、`fb_restored=1`、`users_after=1`）→ `stage=done`（总计约 12 秒）。报告同时记录 `lab126_gui_status_after=lab126_gui stop/waiting`，说明 `start x --no-wait` 已恢复 `Xorg` 与 `/dev/fb0`，但未重启上层 `lab126_gui`，故肉眼看到空白静屏需重启恢复桌面。
- **USB + Touch 组合（`full-quiesce-dryrun.sh` v3）**：在 `udc_state_before=0`、`touch_power_before=on_enabled` 下，执行 **`stage=usb_disconnect uptime=408.92 cpus=0-1`**（往 `soft_connect` 写 `disconnect`）时即卡死并重启（随后再次点击因锁文件残留返回 `refused_test_already_running`）。结合 2026-11-25 `rmmod g_mass_storage` 在同一阶段挂死，两次实机证据一致指向：拔线后 `udc_state=0` 时 ChipIdea OTG（`ci_hdrc.0`）处于低功耗/时钟门控状态，无论 `rmmod` 还是写 `soft_connect` 触碰其寄存器都会挂总线；下一步在主机侧对照 `drivers/usb/chipidea/` 源码核实 `udc_state=0` 是否本身已静止、或是否需先唤醒 runtime PM。

#### ChipIdea UDC（`ci_hdrc.0`）拔线静止源码审计（2026-11-27）

核对原厂 `drivers/usb/chipidea/udc.c` 与 `core.c`、`ci_hdrc_imx.c`：
- 拔线时 `ci_hdrc_gadget_connect(gadget, 0)`（`udc.c:2079-2090`）已顺序执行：`usb_udc_vbus_handler(false)`、`ci->driver->disconnect()`、`hw_device_state(ci, 0)`（向 `OP_USBINTR` 写 `0` 关闭全部 USB 中断）、`_gadget_stop_activity()`（清空全部端点队列）、`pm_runtime_put_sync()` → `ci_controller_suspend()`（`core.c:1136` 进入 LPM、挂起 USB PHY、`imx_disable_unprepare_clks()` 关闭控制器时钟），最后置状态为 `USB_STATE_NOTATTACHED`（`/sys/class/udc/ci_hdrc.0/state` 为 `0`）。
- 随后无论是 `rmmod g_mass_storage` 还是写 `disconnect > soft_connect`，都会进入 `ci_udc_pullup(gadget, 0)`（`udc.c:1684-1699`）并向已门控时钟的 `OP_USBCMD` 寄存器写入，在 i.MX7D 上直接挂死 AHB 总线——这完整解释了 2026-11-25 与 2026-11-27 两次实机挂死。
- 结论：拔线且 `udc/state == 0`、`g_mass_storage users == 0` 时，USB 控制器已由原厂驱动完整静止（中断屏蔽、DMA 排空、LPM + 时钟关闭），交接前应只读核验空闲状态而不应再触碰 `rmmod` 或 `soft_connect`。已同步更新 `full-quiesce-dryrun.sh`（v4）、`boot/koa2/quiesce-verify.{h,c}` 与 `epdc-quiesce.sh`（v4 恢复时补拉 `lab126_gui --no-wait`）。
