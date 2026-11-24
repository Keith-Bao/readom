# KOA2 开发记录

倒序。当前状态见 [STATUS.md](STATUS.md)，设备流程见 [device-runbook.md](device-runbook.md)。
这是一份只追加的记录：写下来的那天知道什么就是什么，后来的更正写在新的条目里，
不回头改旧条目。只有中文版，因为它是工作记录而不是说明。

## QEMU 裸机跳转测试的“缺串口输出”（2026-11-24）

- CONTINUE 记着这个测试在当前分支和基线都缺串口输出。本机复现不出来：直接起 QEMU 能看到
  `params` 地址和 `KO!`，经 monitor 读到状态 `0xa3`、已复制 2 页；`run.py` 单跑和
  `run-tests.py --filter koa2` 里都通过，8 核上再加 32 个死循环也通过。
- 原脚本起 QEMU 后固定 `sleep(2.5)` 再读一次串口文件，读不到地址就报“no parameter address”，
  不带 QEMU 自己的输出。主机忙（例如同时编内核）时这正好表现为“缺串口输出”。这是推断，
  当时的失败输出没有留下。
- 改为轮询串口直到出现地址（上限 20 秒，QEMU 提前退出就停），状态字非零才算读到结论，
  `pass` 再等 `KO!` 或 `X`。失败信息带上 QEMU 是否还在跑和它的输出。
- 防护先证伪一次：把桩内核期望的机型号改成 `0xfffffffe`，测试报
  `FAIL pass: the stand-in kernel did not confirm its registers`。
- 22 项 koa2 主机测试、315 项文档对照、454 处链接全部通过。

## initramfs 的构建不可复现，差的是 7 个字节（2026-11-23）

- 为设备侧两件事重建测试包（11-21 那份里没有标记功能）。包本身核对通过：35 个导入与原厂
  CRC 全部一致、`--mark` 在客户端里、三个脚本 `sh -n` 通过、`sha256sum -c` 自检通过。
- 但 initramfs 的哈希和 11-21 上机那份不一样，而本机这个文件的日期还更早，所以设备上
  跑的那份不是它来的。拆开逐字节比完才确认：内容完全相同，**只差 cpio 头里 `/init` 的
  mtime 7 个字节**。
- 原因是 4.1 的 `gen_init_cpio.c` 里 `-t` 不覆盖文件条目（`cpio_mkfile` 用源文件的
  `st_mtime`，350 行），只覆盖目录和设备节点。脚本本来已经用了 `gzip -n` 和 `-t 0`，
  差的是第三处。已补 `touch -d @0`，并验证：归零后连打两次字节相同，不归零则哈希随
  构建时间变。
- 这件事值得记：测试包用 SHA256 钉住 initramfs、设备脚本加载前核对，一个会自己漂的哈希
  和"内容被改了"在现场分不开。zImage 那边的同类问题（lzop 时间戳）早就记过，initramfs
  这处之前没人查。
- 新增 `koa2-initramfs-repro` 守住三处时间戳。**第一版是空跑的**：它自己把打包步骤重写了
  一遍，于是验证的是它自己那行归零，而不是脚本里的——把脚本里的 `touch` 删掉，它照样通过。
  改成直接跑 `build-smoke-initramfs.sh` 两次（中间动 `init` 的 mtime）才真的守得住：
  删掉脚本里那行，测试失败；恢复，通过。两次都验过。
  这和 11-21 EPDC 那次"空文件留在树里会让检查器误判通过"是同一类错：防护自己要先被证伪一次。

## EPDC 缺口：设备日志里有缺失文件自己打的行（2026-11-23）

- 查 0x04/0x06 时发现，已上传的去标识 dmesg 里有 `mxc_epdc_fb_lab126.c` 自己打的行——
  已发布的 v2 一行都不打，grep 确认过。两块的拷贝区间、`Retrieved voltage control information (0x8c0)`、
  VCOM 从 IDME 到 PMIC 的整条链都在里面。所以这一份波形里 vcd 2240 字节（正好 0x8c0）、
  acd 1086 字节、两块贴在数据末尾，是读出来的，不是推的。原来定的下一步"从 eink flash 读波形
  文件反推"也不需要了：波形是 `/mnt/wfm` 下的 `.wrf.gz`（119650 字节，解压约 3.6 MB）。
- 重新分了一次风险：5 个不能桩的里，错了会伤硬件的是 3 个——`eink_get_wfm_voltage_control`、
  `fetch_epdc_vdc_04_data`、`mxc_set_vcom`。`read_mnt_wfm_v2` 和 `fetch_epdc_adc_06_data`
  源码里看到的后果都在显示上；后者在原厂 `pxp_probe` 里就有默认值 3/21。
- **夜间模式由用户空间直接触发**：`waveform_mode` 等于 `GCK16`/`GLKW16` 就进夜间分支，
  那里 `fetch_epdc_vdc_04_data` 的返回值没人查。第一版要在提交入口拒掉这两个波形。
- 11-21 记的"第二处缺口" `einkwf_panel_*` 没有调用者：全树 .c 里一次都不出现
  （用 .h 做了正对照），不挡重写。
- **草稿里有三处站不住，提交前改了**：把 `mxc_set_vcom` 归成了安全（值好拿，但桩掉它 VCOM
  就没人写）；"定位方式是从尾部往前"是推测，日志只给了结果没给规则；`trc` 一开始当成 14，
  其实 `trt_entries = trc + 1`，所以 `trc`=13，`mc` 日志里没有。
- 工具上的坑：从 Windows 调 `wsl.exe -- bash -lc` 时，命令串里的 `$变量`、`$(...)` 会先在外层
  Linux shell 里展开，工作目录是 `/mnt/e/...` 的 Windows 检出。11-22 提交前那次 DLP 检查因此是
  空跑；已提交内容不受影响——`git show --stat` 全是文本行数，主机测试和 ARM 构建用的也是这些文件。
- `koa2-epdc-gap` 加了从设备日志重算的检查（两块区间首尾相接、vcd 大小等于报告的 0x8c0、
  VCOM 从 IDME 原样到 `mxc_set_vcom`、波形读自文件），并守住文档里"`mxc_set_vcom` 仍然不能桩"
  和夜间模式两个波形。把这两处改掉，测试会失败（验过）。
- 主机测试 65 项通过、0 项失败。

## 跳转标记接上了，三处原来写错的地方（2026-11-22）

- 补上 CONTINUE 里排第一的事：标记的**写入**（`koa2_marker_place`，模块侧 `K2S_MARK`）。
  格式化早就有了，缺的一直是把它放进 printk 缓冲这一步。它不是跳转：不复制载荷、
  不停设备、不下核，`execute_available` 仍然是 0；代价是这次启动日志最后 256 字节。
- **写在活动半区末尾，不是开头。** 下一次启动保存的是活动半区，而开头正是 printk
  自己开始写的地方——写在那里会被当次启动后面的日志直接盖掉。
- **必须自己刷缓存，这一条原来完全没想到。** 翻 PW3 的
  `1043-printk-ring-survives-watchdog-reset.patch` 才发现：这颗 SoC 的看门狗复位不清缓存，
  留在脏行里的标记会在它本要说明的那次复位里一起消失。所以回写函数是参数而不是可选项，
  没有它 `koa2_marker_place` 直接拒绝——写了而出不去，看起来像成功而其实不是。
- **`recovery.md` 原来写的 `__arm_ioremap` 用不了。** 它对 RAM 会 `WARN_ON(pfn_valid(pfn))`
  然后返回 NULL，而这块是内核预留并映射过的 RAM，`pfn_valid` 成立。改用 `phys_to_virt`，
  不导入任何符号。原来那句"用的是已经核对过 CRC 的能力"是错的，已改。
- **符号上踩了两个坑，都会让模块在设备上直接加载失败，和标记本身无关。**
  按地址清缓存（`__cpuc_flush_dcache_area`）编译成 `v7_flush_kern_dcache_area`，
  没有任何原厂模块导入，CRC 核对不了，原厂加载器拒绝整个模块；`get_jiffies_64()/HZ`
  引入原厂没导出的 `__aeabi_uldivmod`，构建脚本当场拒绝。换成 `flush_cache_all`
  （编译成 `v7_flush_kern_cache_all`，`mxc_epdc_v2_fb` 导入过）和 32 位 `jiffies`。
  整模块 35 个导入与原厂 CRC 全部一致，`passed: true`。
  新增 `koa2-marker-wiring` 守这两个坑；把按地址那版改回去，它会失败（验过）。
- 标记活不活得过复位只能在设备上分两次测：放一个、重启、读 `/proc/printkbuf`
  （发布的是上一次启动的缓冲）。KUAL 两项加上了，`marker-test.sh`，**还没上机**。
  它顺带验证 recovery.md 第二节那个只靠源码推断的结论——复位保留 DRAM；
  标记没活下来的话，那一节是错的，比标记本身要紧。
- 主机测试 65 项通过、0 项失败。

## EPDC 缺口量化（2026-11-21）

- 文档里"梳理 v2 全部外部调用"一直没做，所以替代实现的工作量没有数。量法：给缺失文件放一个
  空文件，让编译器报缺的符号，然后**把空文件删掉**——它留在树里会让 check-kernel-source.py
  对 stock 档位误判通过，编出一个没有波形解析、没有电压控制的显示驱动。
- 结果：缺失文件要提供 **9 个函数**，另需 `#include <linux/proc_fs.h>`。
  其中 5 个在面板供电和波形算法路径上（`read_mnt_wfm_v2`、`eink_get_wfm_voltage_control`、
  `fetch_epdc_vdc_04_data`、`fetch_epdc_adc_06_data`、`mxc_set_vcom`），值错了有物理后果；
  另外 4 个只影响日志和调试画面，可以先桩掉。这个区分之前没有。
- `fetch_epdc_adc_06_data` 填的 `reagl_sft`/`reagl_asv` 直接喂 PxP 的 WFE-B 硬件；
  `mxc_set_vcom` 用的是 IDME 里的 `idme_vcom_value`，每台设备不同。
- **发现第二处缺口**：`include/linux/einkwf.h` 发布了并声明 `einkwf_panel_*` 9 个函数，
  但整个包里没有任何 .c 实现它们。缺的不是一个文件，是两处。
- **比原先记的乐观一处**：波形容器格式不在缺失文件里。`struct waveform_data_header`
  定义在已发布的 v2 源码第 462 行，字段齐全（checksum、file_length、mc、trc、
  advanced_wfm_flags、xwia/wmta 偏移）。真正未知只剩 0x04/0x06 两个子块的内部布局，
  包里搜不到任何描述。
- 新增测试 `koa2-epdc-gap`：守住"树里不能留空桩"（这个危险我自己制造过又删掉）、
  stock 档位仍拒绝、headless-smoke 仍放行，以及文档里那两份函数清单还在。
- 主机测试 64 项通过、0 项失败。

## 恢复路径分析与跳转标记（2026-11-21）

- 补上 ROADMAP 里那条硬门槛要求的东西：交接失败怎么回来、怎么知道为什么
  （[recovery.md](recovery.md)，中英双语）。
- 查明失败后日志为什么还在：`imx2_wdt` 带 `fsl,wdog_b`，超时经 WDOG_B 到 MAX77796 的
  reset 输入；PMIC 复位分 Full 和 Partial 两类，复位原因表里 `RSTIN_PRSTRT`
  （Reset Input Partial Restart）对应 `reset=watchdog`，所以看门狗走的是 Partial Restart，
  不是断电级的 Full Restart。Lab126 把 printk 缓冲放 DRAM 跨启动用，也只有在这个前提下成立。
- 查明读日志的机制：原厂内核每次启动把 0x9be00000 的上一次内容复制到 0x9be40000 再清空前半段，
  `/proc/printkbuf` 读的就是那份备份，即上一次启动的日志。两段都在持久排除表里，有测试守着。
- **发现一个静默失败模式**：新内核若在 printk 初始化之前就死（解压中、head.S、FDT 读不成），
  0x9be00000 里仍是原厂上一次的日志，复位后看起来和"什么都没发生"一样。
  解决办法是跳转前写一个标记。格式化已实现（`boot/koa2/marker.c`，测试 `koa2-marker`，
  覆盖所有过短缓冲——写过界会毁掉它本要保住的那份日志）；写入属跳转阶段，还没接上。
- 更正 CONTINUE.md 的下一步：原来写的两件事（硬件 run/undo、EPDC 帧缓冲排除）一个改到了
  用户空间、一个因 EPDC 卸载会自己释放帧缓冲而不必做。
- 主机测试 63 项通过、0 项失败。

## 静止阶段：停设备改到用户空间（2026-11-21）

- 先查能用的符号：拿 63 个原厂模块核对，发现能停设备的符号基本都不能用——构建树不导出
  `clk_disable_unprepare`/`dmaengine_terminate_all`/`mmc_suspend_host`/`usb_gadget_disconnect`；
  `dpm_suspend_start`/`device_release_driver`/`fb_set_suspend` 没有原厂 CRC；
  `lock_fb_info` 的 CRC 与原厂不一致，碰 framebuffer 就会被拒绝加载。
- 又发现一个更硬的障碍：停下来的设备寄存器读不得。EPDC 驱动碰寄存器前先开时钟、卸载时关，
  i.MX7 上时钟 gate 之后读外设寄存器会挂总线。所以"读引擎状态确认空闲"这种检查
  恰恰会在静止成功时把设备挂死。PW3 的 engine-guard 是在设备还活着时读的，这里不适用。
- 于是改设计：停设备交给用户空间走内核自己的卸载/下线路径（可逆、内核为此而写），
  模块只用有原厂 CRC 的符号检查结果，不读外设寄存器。`cpu_down` 因此不必导入
  （它没有原厂 CRC，核对不了）。
- 顺带解决旧问题：EPDC 的 remove 路径会释放 DMA 帧缓冲，所以"交接当次读出 EPDC 帧缓冲
  地址加入排除区"这件事不必做了，那几段变回普通空闲页。
- 新增 `quiesce-dryrun.sh`（停四个主设备、sync+只读重挂、下次核，然后全部放回）、
  `quiesce-verify.c`（模块侧判定）和两个测试；套件多了一个 KUAL 菜单项。
- eMMC 停不了，它是根文件系统，这是残留风险，写进文档而不是假装解决。
- 主机测试 62 项通过、0 项失败。模块仍是 31 个导入全部与原厂一致。

## 公司电脑续作（2026-11-21）

- 拉取家里推上来的实机结果：暂存测试 `sealed_plan_valid`，payload 1706 页、避开 230 页、
  复制表 2 页 1710 项、fdt_bytes 47972、`execute_available=0`、insmod/rmmod 都是 0。
- 用设备拷回的 63 个原厂模块离线核对 CRC：31 个导入和 `module_layout` 全部一致，
  所以之前"最可能 insmod_refused"的预期是错的，原因见 stock-module-abi.md。
- 查明构建树与原厂 12 个符号不一致：9 个 framebuffer 来自关掉 EINK 面板；
  3 个调度符号来自 `CONFIG_FALCON_WRAPPER`（`kernel/sched/core.c` 在该选项下 include
  `falcon_storage.h`，genksyms 把它算进该文件导出符号的 CRC）。实验验证：打开后三者
  变成原厂值，framebuffer 九个不变。
- 新增 `build-stock-kernel.sh module-abi` 配置：headless-smoke 加 FALCON_WRAPPER，
  只编 vmlinux+modules，专供模块编译针对；载荷仍用 out-headless-smoke。
  套件脚本已按这个分工改好。
- 新增测试 `koa2-stock-abi` 守住这些事实，并把"`cpu_down` 核对不了"写成用例。
- `tools/private-data-allow.txt` 收录 materials 目录：29 处命中全是二进制里上游作者/厂商
  的地址，设备标识不在其中（dmesg 上传前已去标识）。
- 主机测试 60 项通过、0 项失败。

## 文档整理（2026-11-21）

- 新增中英文换电脑继续入口，区分主机、模拟器和实机的证据与下一步。
- 修正旧的未上机/预期拒绝描述、盘符和套件数量；静止顺序说明与 quiesce.h 对齐。
- 上一条的“不纳入 Git”指原始模块与完整日志；经检查的精简 report.txt 和套件哈希已按用户要求上传。
- 本轮仅修改文档，没有新设备操作或新内核启动。

## 实机暂存检查通过（2026-11-21，Asia/Shanghai）

- 代码 f858117；报告标识 20261120-160050-26549（UTC 时间，对应本地 2026-11-21 00:00:50）。
- 设备身份与套件哈希检查通过；原厂模块回收 63 个。
- insmod_exit=0；state=2、plan_valid=1、execute_available=0；client_exit=0、rmmod_exit=0。
- fdt_bytes=47972、payload_pages=1706、collision_pages=230、list_pages=2、list_entries=1710。
- seal_stage=0、seal_result=0；result=sealed_plan_valid。tainted 前后均为 4097。
- 原始报告与模块保存在 ~/koa2-evidence/stage-test/，另备份至 Windows F:/Kindle/koa2-evidence/stage-test/；不纳入 Git。
- 实机加载成功，覆盖先前对 insmod_refused 的预测；不据此推断所有模块均兼容。
- 未跳转、未启动新内核。按流程在此停止；后续缺静止顺序的硬件 run/undo 和交接当次 EPDC 帧缓冲排除。

## 第二台主机重建（2026-11-19，覆盖下文的主机路径记录）

- 新主机 WSL 环境已建立，细节见 build-environment.md「第二台主机」。
- headless-smoke 在本机从源码重建：make 退出 0，
  release=4.1.15-lab126-koa2-headless-smoke，97 个模块 vermagic 全部匹配
  （identity.json passed）。日志 ~/artifacts/headless-build-this-host.log。
- DTB SHA256 与上一台主机一致：8df4fe5d…f144d9。
- zImage 哈希与上一台主机不同的原因已查明（2026-11-20，见 headless-smoke.md 末节）：
  与内核无关。`cat Image | lzop -9` 经管道压缩时 lzop 把当前时间写进头部，所以每次构建的
  zImage 都不同；链接器的 GNU build-id 又由目标文件路径算出，换输出目录就变。
  本机在另一个输出目录完整重建一次：Image 只差那 20 字节 build-id，清零后哈希完全相同，
  piggy.lzo 解压后与 Image 逐字节一致。**内核本身可复现**。
  判断工具为 ports/koa2/compare-kernel-build.py，测试 koa2-compare-build。
- 主机回归：50 passed、0 failed、24 not run here，与上一台主机相同。
  日志 ~/artifacts/host-tests-this-host.log。
- 只读 Boot Preflight 已由用户在 Kindle 上运行（20261119-044638-4937），结果见
  boot-preflight-findings.md：可用 RAM 两段、EPDC/ramoops/printk/memreserve 保留区、
  2G/2G 且 lowmem 恒等、运行时 DTB 与编译 DTB 的板级差异、原厂会直接写 MMC 原始扇区。
  原始数据只在 ~/koa2-evidence/，未进仓库。
- 排除表已写进 boot/koa2/contract.c（koa2_measured_ram/excluded，7 段），并新增运行时 FDT
  memreserve 读取与"每个 memreserve 必须被排除表覆盖"的检查；新的 bootloader 保留会被拒绝而不是覆盖。
  ASan/UBSan 主机测试、qemu-arm 静态测试、运行时 DTB 与编译 DTB 全部通过。
- memreserve #1 已查明是 FDT 本身（U-Boot fdt_shrink_to_minimum）；审计中另发现 Falcon BIOS
  0x9ffc0000–0x9fffffff 必须保留，已补进排除表。
- __dump_lastk_to_mmc 的三个调用方与目标扇区已审计（见 boot-preflight-findings.md 末节）：
  原厂每次经看门狗重启都会写 eMMC 扇区 0x9a400 起 256 KiB。交接不得走重启路径。
- 静止顺序与 eMMC 路线见 handoff-quiesce.md：源码上 mmcblk2 可改用标准 sdhci 驱动（usdhc3
  compatible 改回芯片 dtsi 的标准值）；关 FALCON_BLK 需要先让 dump_lastk_to_mmc 成为空操作。
  看门狗交接后仍在计时（60 s，WDOG_B），可作新内核卡死时的自动回退；次核用原厂 cpu_down(1)。
- 实验内核已去掉 Falcon 存储路径并重建通过（见 headless-smoke.md 顶部）：eMMC 交给标准
  sdhci-esdhc-imx，dump_lastk_to_mmc 不再写 eMMC；97 个模块 vermagic 匹配。
- 需要静止的 DMA 主设备已列在 handoff-quiesce.md 第五节；EPDC 帧缓冲是动态分配的
  （本次在 0x99000000 一带），交接要先停 EPDC 或在当次读出它的地址。
- 最小 smoke initramfs 已写好（smoke-initramfs.md）：PID 1 不碰任何块设备，写
  KOA2_SMOKE 标记到 /dev/kmsg，占用 /dev/watchdog 一段时间后停止喂狗，由看门狗复位回原厂。
  命令行解析拆成 smoke-params.h，17 个用例登记为 koa2-smoke-params，ASan/UBSan 与 qemu-arm 均通过。
- 查明 imx2_wdt 在 LAB126 下由内核定时器喂狗，直到 /dev/watchdog 被打开；所以"用户空间卡住
  会自动复位"不成立，必须由 init 接管看门狗。handoff-quiesce.md 第二节已据此更正。
- 交接计划校验已写（boot/koa2/plan.c，README「交接计划校验」）：derive-kernel-footprint.py 从构建
  目录量出 LZO + AUTO_ZRELADDR 内核的解压占用（0x80004000–0x80c56000），8 条规则检查 zImage/FDT/
  initramfs 的目的地址；测试登记为 koa2-plan。smoke 布局（0x80d00000 / 0x81400000 / 0x81500000）
  对构建 DTB 通过；实机运行时 DTB 因保留原厂 FDT/initrd 的 memreserve 被拒绝，交接需要改写 FDT，尚未实现。
- 交接 FDT 改写已写（boot/koa2/fdt-rewrite.c，README「交接 FDT 改写」），测试为 koa2-fdt-rewrite。
  做法：清空 reserve map，替换 /chosen 的 bootargs 和 initrd，其余原样复制。
  对实机运行时 DTB 的结果：改写后计划校验通过；dtc 对比只有预期的差异。
  这只是主机层面的结论，没有在设备上验证。
- 交接暂存模块已写（boot/koa2/stage.c，README「交接暂存模块」）：暂存、核对、改写 FDT、校验计划，
  不含跳转。主机测试 koa2-stage 用替身内核接口跑 stage.c 本身；对 headless-smoke 树编译出
  koa2_handoff_stage.ko（33 个导入，导出前会检查不含 GOT、不写协处理器、不导入下线/重启类符号）。
  阻塞：原厂内核树因缺 EPDC 源码编译不出，没有原厂 Module.symvers，模块的 vermagic/CRC 对不上
  原厂内核，暂时不能在设备上加载。下一步是从设备上的原厂模块读出导出符号的 CRC。
- 原厂 CRC 核对已写（ports/koa2/module-crcs.py，README「原厂内核的版本检查」），测试为 koa2-module-crcs。
  modversions 下 release 字符串不同不会被拒，只看 vermagic 尾部和每个导入的 CRC。stage.c 换掉了
  原厂模块没用过的 4 个导入，现在 31 项导入都能核对。Boot Preflight 升到 v2，会只读复制原厂 .ko。
  下一步：用户在 Kindle 上跑一次 v2，再用 KOA2_STOCK_MODULES 重建模块。
- 设备测试套件和回家步骤已写（ports/koa2/stage-test/、build-stage-test-kit.sh、device-runbook.md）：
  KUAL 一键 insmod → 暂存 → 封存 → rmmod，同时只读复制原厂 .ko，不含跳转。主机上验证过两种拒绝路径：
  内核不符时在身份检查处停下，套件被改动时在哈希检查处停下。
  暂存模块被避开的页数上限改为 8192 页（32 MiB），超出时返回 -ENOSPC，避免挤占原厂系统的内存。尚未在设备上运行。
- 复制表已写（boot/koa2/copylist.c，README「复制表」），测试 koa2-copylist 会按跳转阶段的方式
  走一遍写出来的表并核对每个字节。暂存模块封存时会建出真实的表并审一遍（seal_stage 10），
  但没有任何代码会走它。STATUS 多了 list_pages/list_entries。
- 跳转蹦床已写（boot/koa2/jump.S，README「跳转蹦床」），没有编进暂存模块。
  KOA2 不需要 PW3 那样的私有页表：2G/2G 下 lowmem 本来就是恒等映射。
  测试 koa2-jump-baremetal 在 QEMU 的 Cortex-A7 上以裸机方式跑真正的 jump.S 和复制表：
  正常路径里替身内核自己核对 r0/r1/r2，五种拒绝从 monitor 读参数块核对。
  这只说明 ARMv7 上按设计运行，不说明真机外设行为；蹦床从未在设备上执行。
- 静止顺序已写成可测的序列（boot/koa2/quiesce.c，README「静止顺序」），测试 koa2-quiesce：
  十步、不可逆点在"关缓存"，对每一步都试一遍失败并核对逆序回滚，拒绝"可逆步骤没有 undo"
  和"不可逆步骤宣称 undo"两种表。真正碰硬件的 run/undo 还没写，模块里也还没装这张表。
- 当前 headless-smoke 产物（2026-11-20 重建）：Image build-id 清零后 e2eba61b8420…，
  zImage 4921c4b2…（每次构建都会变，钉在 target-lock.h 里）。测试套件 58→60 项全过。
- QEMU 整机启动这条路走不通并记录了原因：QEMU 6.2 的 i.MX7D 模型缺 OCRAM_S、DDR 低功耗
  OCRAM、GPCv2 core-1 上电握手和 DDR 控制器，绕过后 imx7_pm_common_init 又要用被去掉的 OCRAM，
  形成循环依赖。run-qemu-smoke.py 留作记录，不计入测试。
- 仍没有任何新内核上机，交接仍只有校验、没有跳转。下一步（主机侧）：交接模块的校验阶段
  （只读 FDT + 排除表 + 载荷位置，失败即退出），以及 EPDC 帧缓冲地址的运行时读取。


## 最新进度（覆盖下文早期 WSL 阻塞记录）

- 当前完整主机回归：50 passed、0 failed、24 not run here。
  日志 /home/builder/artifacts/host-tests-current.log；Linux 副本已从 Git
  快进到 7772e24，避免 Windows CRLF 覆盖 shell 文件。
- 原始上游 GPL 补丁中的 3 个公开 sign-off 邮箱已按两个确切文件路径
  加入既有隐私扫描允许清单；没有放宽其他文件或设备数据规则。
- 本轮已自动提交主机契约、预采集包、Binder 4.1 适配及 QEMU IPC、SELinux
  policy30 编译研究。后续自主工作优先为 SELinux 策略运行测试与完整集成准备。

- QEMU Binder 继续扩展并通过：同步请求/回复、实际 FD 数据读取、
  BC_TRANSACTION_SG 的 PTR 缓冲区重定位与数据完整性；无内核告警。

- QEMU 双核内核运行验证已通过：三 Binder context 独立管理、协议/mmap、
  子进程实际 one-way payload 分发与回收，运行日志没有 WARNING/BUG/Oops/panic。
  测试曾捕获页映射 bug，已改为官方 4.1 映射路径。见 qemu-binder.md。
- SELinux policy30/xperms 与 ioctl 审计前置补丁来自官方固定提交，独立树
  对象编译通过；策略加载与执行行为未验证。见 selinux41.md。
- 原厂 printk 的 dump_lastk_to_mmc 包含原始 MMC 写入入口。设备实验前
  必须审计/隔离此路径；当前没有任何新内核上机。
- 当前下一步：扩大 Binder 同步/FD/SG 运行测试；policy30 行为测试；
  真机后端仍等待原厂运行时 FDT/保留内存的只读报告，不能猜地址直接跳转。

- 主机自主工作新进展：android41-core.config 46 项原生配置全部生效，
  namespace/seccomp/SELinux/ashmem/原生 Binder/LMK 对象编译通过。
- 已从仓库的可追溯补丁重建 Binder 多 context/SG/安全修复实现，针对 4.1
  FD、kuid、PID namespace、hlist、map_vm_area 做适配；46+1 配置和对象编译通过。
  未做 Binder 运行时验证，尚未支持 SELinux policy30。见 android41-audit.md。
- 下一项可独立工作：QEMU 中建立真实 Binder 三 context 隔离及 ioctl smoke；
  同时真机交接仍等待用户方便时运行已部署的只读 Boot Preflight。

- 已新增 boot/koa2/contract.c：只读 FDT 与内存区间校验；ASan/UBSan 下
  截断/越界/机型/区间测试及 20000 变异测试通过；ARM 静态版 QEMU 也通过。
- KUAL 的 Readom KOA2 Boot Preflight 已准备并部署，未由代理执行。
  用户方便时启动一次，报告保存在 koa2-boot-preflight/；原始数据不可上传 Git。
- 编译 DTB 缺 bootargs，内存比实机可用范围宽，不能直接拿来交接。
  真实交接前必须取得并核验原厂运行时 FDT、保留范围和日志行为。
- 用户睡觉期间继续主机开发/测试/自动提交，所有设备跳转仍未执行。

- 最新：实验源码已独立解压到 linux-headless-smoke，原厂 linux-stock 不变。
  setlocalversion 条件修正只识别实验后缀；4 项实际 shell 隔离测试通过。
- 已重建成功，release=4.1.15-lab126-koa2-headless-smoke，97 个模块的
  ELF .modinfo vermagic 全部匹配。保守 ARM 重定位检查通过，未做实机加载。
- zImage SHA256：4c69f47ef82d26ef76d2b1701bc7b32de9715f528fc899ba3124e36140de3f6e。
- 日志：/home/builder/artifacts/headless-identity-build.log、headless-identity.json、
  headless-relocations.json。旧原厂后缀产物仍仅作历史证据，不可部署。
- 当前下一步：实现 DTB/内存契约的主机校验与只读设备校验；核实 stock
  模块 ABI、SMP 次核退出、DMA/中断静止和日志保留后才准备跳转实验。
- 原厂 cpu_down 有导出，hotplug 的 i.MX7D 路径包含 GPCv2 次核电源控制；
  只读源码审计不等于次核可以安全下线或支持恢复。尚无可用交接模块。

- 当前主路线已按用户要求改为 headless-smoke：先解决无显示新内核和交接。
- 独立 profile 已编译/导出一次 zImage、DTB、模块，make 退出 0。
- 当前产物仍不能部署：官方 setlocalversion 强制原厂 release，实验后缀
  未生效。已新增导出拒绝检查。详情和旧产物哈希见 headless-smoke.md。
- 当前下一步：独立实验版本标识补丁和重建；随后模块检查、DTB/SMP 校验及日志通路。
- 不再把取得完整 EPDC 源码当作启动链研究的前置条件，但完整 Android 的显示目标保留。

- 最新路线决策见 route-decision.md：原厂内核缺 namespace/seccomp/LSM，
  只补普通模块无法达到约定目标。已排除该候选，继续新内核路线。
- EPDC 已核实 8 组直接接口，旧版同名函数存在签名差异且缺少 v2 电压入口；
  尚未完成兼容实现，不能直接移植旧文件。下一步先恢复波形/电压数据契约。

- WSL 安装完成：Readom-KOA2 已在 E 盘导入 Ubuntu 22.04.5。
- Linux 8 CPU / 10GB RAM / 8GB swap；依赖安装完成，可直接联网。
- Linux Git 副本 /home/builder/readom-work/readom；源码 linux-stock。
- ARM GCC 4.9 可执行，归档来源及独立校验值见 build-environment.md。
- 完整上游主机测试：49 passed、0 failed、24 skipped，日志
  /home/builder/artifacts/host-tests.log。Linux KOA2 EpdcAbi.h 语法检查通过。
- C 已开始，build-stock-kernel.sh 可重复执行；host dtc 的 yylloc 问题
  以 make 命令行 HOSTCFLAGS=-O2 -fcommon 解决，仅影响主机工具。
- C 当前真实阻塞：mxc_epdc_v2_fb.c:792 包含 mxc_epdc_fb_lab126.c，
  官方内核归档和已解压源码里未找到该文件；编译退出 1，不能宣称内核通过。
- 没有关闭 EPDC、修改原厂驱动、加载模块或进行真机交接。

### 当前下一步

1. 审计官方同机型其他固件归档及公开源码镜像，寻找缺失的 Lab126 EPDC
   包含文件，核实来源、许可证和与 v2 调用点的兼容性。
2. 若能取得完整源文件，以单独补丁和来源记录补齐；若不能，先列出
   所有缺失接口和重实现范围，不能拿空 stub、关闭 LAB126 或换原厂二进制冒充源码复现。
3. 原厂构建通过后才进行模块审计和干净重建，然后推进 D。
4. 产物日志位于 /home/builder/artifacts/stock-build-console.log 和 stock-kernel/。

---

以下为首次安装前的历史记录，不代表当前阻塞。

## 目标及起点

执行 ROADMAP.md 的完整自用目标。分支 port/koa2，起点 6a74732。
官方内核归档校验信息在 source-lock.json；原始采集位于电脑
F:\Kindle\koa2-evidence，第二次采集在 v2 子目录。

## 已完成

- A：路线与持续交接记录已建立；硬件与源码审计见现有文档。
- B 前置检查：Windows 11 Pro 26100，约 16GB RAM、16 逻辑处理器。
- WSL --status 和 --version 均报告尚未安装 WSL；当前 PowerShell 非管理员。
- CPU 的 VirtualizationFirmwareEnabled=True；CIM 的 SLAT 返回 False，
  尚不能据此断定不可用，需要安装后实际验证 WSL2 启动。
- 准备 install-wsl-admin.ps1，拒绝非管理员运行；只安装 WSL 基础组件，
  不自动重启、不安装默认 Ubuntu、不注销任何发行版。

## 当前阻塞及用户操作

需要 Windows 管理员安装 WSL 基础组件。用户以管理员身份运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File F:\Kindle\readom\ports\koa2\install-wsl-admin.ps1
```

ExecutionPolicy Bypass 仅针对此次进程，不改系统持久策略。
按安装输出决定是否重启；保存失败输出以便诊断。

## 下一步（GPT 接手）

1. wsl --version / --status 验证基础安装和 WSL2；若虚拟化失败先定位错误。
2. 读取并保留已有 .wslconfig，再设置 8 CPU、10GB RAM、8GB swap。
3. 从 Ubuntu 官方源下载并校验 22.04 rootfs，导入 E:\Readom-KOA2，
   发行版名 Readom-KOA2，版本 2；在 ext4 内建立 builder 与开发目录。
4. 安装依赖、验证代理和 ARM 工具链；先完成 B 的主机/ARM 冒烟测试。
5. 实施 C 的独立内核构建脚本，源码从 F:\Kindle\koa2-sources 输入。

## 尚未验证

没有 Linux 依赖安装、ARM 构建、内核加载或设备跳转。
当前不能宣称 B/C 完成，也没有可安装的 Android 包。

## 2026-11-24 触摸单项休眠与唤醒（主机）

追踪原厂 `cyttsp5_core.c` 的休眠、IRQ、watchdog 和 startup 路径。新增 `touch-quiesce.sh`（KUAL：Touch only）及 `koa2-touch-quiesce` 主机测试：以醒着时的 i2c2 中断为对照，休眠窗口要求总线和触摸中断为 0，调节器、供电 GPIO 和中断线程状态独立核对，不依赖 `sleep_status`。没有连接设备。

## 2026-11-24 EPDC 引用来源（主机）

源码追踪：帧缓冲每次 open 取一次模块引用，映射保留引用；固件请求已在启动时归还，proc/sysfs 不取引用。预检新增 `epdc` 模式（KUAL：Inspect EPDC framebuffer users），只读核对打开者与引用数。没有连接设备。

## 2026-11-24 回退与早期日志审计（主机）

复核实验内核（`out-headless-smoke`）与 `smoke-init.c` 的回退和早期日志条件，更新 `recovery.md`/`recovery.en.md` 与 `CONTINUE.md`/`CONTINUE.en.md`。确认 `printk_reserve_buf()` 先于 FDT 保留扫描、`imx7d_reserve()` 落在 `0x9bf00000+0x100000`、`dump_lastk_to_mmc()` 为空操作、`pstore` 走 `pgprot_writecombine`；同时同步 `imx2_wdt` 内核定时器的兜底边界，并查出 `log_goal_first`（`0x9be00000`）写入与清零无 D-cache 回写，须在真机跳转前补齐。主机 `koa2` 22 项测试、文档配对与链接检查全过。没有连接设备。

## 2026-11-24 实验内核 printk 缓冲 D-cache 回写（主机）

在 `ports/koa2/prepare-headless-source.py` 中为 `kernel/printk/printk.c` 的 `log_goal_first`（`0x9be00000`）补充 `log_goal_write_back()`（`__cpuc_flush_dcache_area` + `outer_clean_range`），覆盖 `setup_log_buf()` 的备份复制、清零与每次 `log_store()` 写入；新增主机测试 `koa2-printk-writeback` 并重新编译 `out-headless-smoke` 验证通过（`printk.o` 已链接 `v7_flush_kern_dcache_area` 与 `outer_cache`）。未改动已封装校验的 2026-11-24 真机套件，没有连接设备。
