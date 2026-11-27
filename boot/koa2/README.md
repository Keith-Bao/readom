# KOA2 交接

在原厂内核里把新内核装进内存、再跳过去的那套代码。不写分区、不改引导。
English: [README.en.md](README.en.md)。设备流程见
[ports/koa2/device-runbook.md](../../ports/koa2/device-runbook.md)。

**现在的状态**：暂存和校验的全部代码已完成，并有主机测试；跳转蹦床已写好并在
QEMU 的 Cortex-A7 上裸机跑过，已编进**单独的执行模块**，由空跑装好但不执行（见下文“执行模块”）；
静止由用户空间脚本完成，各外设都已在实机上停下并恢复过，模块只核验结果。**2026-11-21 实机暂存检查通过；尚未在设备上执行跳转。**

## 顺序

| 阶段 | 做什么 | 在哪 | 可逆 |
|---|---|---|---|
| 1 | 核对原厂内核身份 | `stage.c` 加载时 | 是（不加载） |
| 2 | 暂存 zImage、原厂 FDT、initramfs 到自己拥有的页 | `stage.c` BEGIN/PUT | 是（关闭即释放） |
| 3 | 核对哈希、钉住的内核、FDT 身份与保留区 | `stage.c` SEAL、`contract.c` | 是 |
| 4 | 算出布局并校验 | `plan.c` | 是 |
| 5 | 改写 FDT（bootargs、initrd、清 memreserve） | `fdt-rewrite.c` | 是 |
| 6 | 建出复制表并审计 | `copylist.c` | 是 |
| 7 | 静止：EPDC、Wi-Fi、USB、SDMA、eMMC、次核、中断、看门狗 | 用户空间脚本停设备（`exec-dryrun.sh`），`quiesce-verify.c` 核验；顺序表在 `quiesce.c` | 每步都要能回滚 |
| 8 | 关缓存、关 MMU、走表、跳转 | `jump.S`（编进执行模块，空跑装好不执行） | **不可逆** |

第 1 到 6 步就是设备测试现在做的事，做完就卸载。不可逆点是第 8 步开始时的“关缓存”：
在那之前任何一步失败都必须把原厂系统放回原样；在那之后已经没有可以回去的原厂内核，
只剩新内核或看门狗两条路。

## 这里的代码怎么测

交接这件事不能在主机上整体验证，所以分三层：

- **主机测试**（`tools/run-tests.py --filter koa2`）：契约、布局、FDT 改写、复制表、
  静止顺序、CRC 核对，以及用替身内核接口跑 `stage.c` 本身。全部开 ASan/UBSan。
- **裸机 QEMU**：`jump.S` 在 Cortex-A7 上真的关 MMU、走表、跳转。
- **设备**：只有暂存和校验这一段，由用户按 runbook 发起。

QEMU 整机启动这条路走不通，原因记在
[ports/koa2/STATUS.md](../../ports/koa2/STATUS.md)：QEMU 6.2 的 i.MX7D 模型缺
OCRAM_S、DDR 低功耗 OCRAM、GPCv2 的 core-1 上电握手和 DDR 控制器。

## 契约校验（contract.c）

只读校验 DTB 结构与 Zelda 机型，以及内存区间。它拒绝截断、重叠块、无终止 reserve map、
深度过大、非法 token、错误的 compatible、溢出的区间，以及跨越允许 RAM 边界的载荷。
它不是 libfdt 的替代品，也不验证面板校准或"可以跳转"。

实测的内存布局（`koa2_measured_ram`、`koa2_measured_excluded`）是**这一台设备**的事实，
不是默认值，来源见
[ports/koa2/boot-preflight-findings.md](../../ports/koa2/boot-preflight-findings.md)。

主机测试包含 20000 次确定性变异输入；另以 ARM 静态二进制在 qemu-arm 上跑同样的测试，
以及实机运行时 DTB。

## 交接计划校验（plan.c）

plan.c 只判断"把 zImage、FDT、initramfs 放到哪里"是否成立，不分配、不复制、不跳转。
暂存页由交接模块从运行中的分配器取得，最后在 MMU 关闭后复制到目的地址；因此目的地
可以覆盖原厂内核、原厂 FDT 和原厂 initrd，但不能碰跳转后仍要存在的区段
（koa2_handoff_persistent：printk 缓冲、ramoops、EPDC 保留区、Falcon BIOS）。

内核占用由 ports/koa2/derive-kernel-footprint.py 从构建目录读出（LZO + AUTO_ZRELADDR，
不能用 PW3 的 gzip 脚本）。headless-smoke 构建：zreladdr 0x80008000，页目录
0x80004000 起，Image 12416856 字节，_end 0x80c55a0c（含 BSS），zImage 运行时占用
加载地址之后 6722520 字节（映像、BSS、栈、64 KiB malloc）。

规则：
1. 段页对齐，整段落在一段实测 RAM 内（512 MiB 全是 lowmem），不碰持久区段。
2. 恰好一个 zImage、一个 FDT，最多一个 initramfs。
3. 占用数据与此 zImage 一致：zreladdr = (加载地址 & 0xf8000000) + 0x8000。
   zImage 必须在 RAM 的头 128 MiB 内，否则新内核把自己的 128 MiB 对齐基址当作
   PHYS_OFFSET，丢掉下面全部 RAM。
4. zImage 放在内核 _end 之上，head.S 不会自我搬移，解压也写不到自己。
5. FDT 不碰内核与运行中的 zImage，且落在 head.S 预先映射的两个 1 MiB 段内。
6. initramfs 不碰内核、zImage、FDT；7. 各段互不重叠。
8. 交给新内核的 FDT 里，每条 memreserve 都必须是持久区段、FDT 自身（按页取整）或
   initramfs。原厂的 FDT/initrd 条目会让新内核预留已经失效的内存，也说明传过去的是
   没改写过的 FDT。（`arm_memblock_init` 先处理 initrd 再扫 memreserve，所以这些条目
   不会让 initramfs 被丢弃；早先记成会，是搞错了顺序。）

koa2_plan_propose 给出的 headless-smoke 布局：zImage 0x80d00000，FDT 0x81400000，
initramfs 0x81500000；用构建 DTB（0 条 memreserve）校验通过。用实机运行时 DTB 校验
被拒绝（规则 8，两条原厂 memreserve），这是预期结果：交接必须传一份改写过的 FDT
（去掉原厂 memreserve、改 /memory 为实测两段、写 /chosen 的 bootargs 和 initrd），
改写见下节。

## 交接 FDT 改写（fdt-rewrite.c）

koa2_fdt_rewrite 把原厂运行时 FDT 逐 token 复制成新 FDT，只改三处：
- header reserve map 换成调用方给的列表（smoke 用空表）；
- /chosen 的 bootargs、linux,initrd-start、linux,initrd-end 换成新值（无 initramfs 时两条
  initrd 都删掉），新属性写在 /chosen 开头、子节点之前；没有 /chosen 就在根节点末尾补一个；
- 删掉 NOP。缺少的属性名追加到 strings 块末尾。

/memory 原样保留：U-Boot 写的是 0x80000000 + 0x20000000，实测的 0x9c000000 空洞来自
reserved-memory/epdc_reserved 的 no-map，这个节点也原样保留。

空 reserve map 足够，原因如下：
- 新内核在 early_init_fdt_scan_reserved_mem 里自己预留 FDT；
- initramfs 由 /chosen 预留；
- printk 缓冲（printk_reserve_buf）、ramoops（imx7d_reserve）和 Falcon BIOS（falcon_mem_reserve）
  由内核代码预留；
- EPDC 由 reserved-memory 预留。

失败时不留下半成品（*bytes 为 0，已写部分清零），输出必须再次通过 koa2_fdt_validate。
不分配内存，可以原样放进内核模块。

测试 koa2-fdt-rewrite 覆盖了以下情况，用 ASan/UBSan 运行：
- 替换 /chosen，以及没有 /chosen 时补一个；
- 第二遍改写结果逐字节不变；
- 所有长度不足的输出缓冲；
- 越界的编辑参数；
- 20000 次变异输入。

check-plan.c 对真实文件走完整流程：先建议布局，再按布局改写 FDT，最后校验计划，只写出新 FDT。
在实机运行时 DTB（49280 字节）上的结果：
- 新 FDT 47972 字节，0 条 memreserve，计划校验通过；
- 布局为 zImage 0x80d00000、FDT 0x81400000、initramfs 0x81500000–0x81542548；
- 用 dtc 反编译后与原件对比，只有两条原厂 memreserve 和 /chosen 三个属性不同。

产物只存在 ~/koa2-evidence/handoff-plan/，不进仓库。

2026-11-21 实机暂存已验证 FDT 改写与布局校验；不包含复制和跳转。

## 交接暂存模块（stage.c，koa2_handoff_stage.ko）

模块只做暂存和校验，不含跳转。实验模块出错仍可能使原厂内核卡死或重启。没有任何 ioctl 会把载荷复制到目的地址、停设备、
下线次核或离开 Linux；关闭设备文件就释放全部暂存页，原厂系统不受影响。
复制、静止和跳转是下一阶段，要另起一个设备、另设门槛。

流程（/dev/koa2-handoff-stage，需要 CAP_SYS_BOOT，同时只能打开一次）：
1. 加载时核对原厂内核身份：release `4.1.15-lab126`、version
   `#1 SMP PREEMPT Tue Aug 1 11:50:12 UTC 2023`、armv7l、PHYS_OFFSET 与 PAGE_OFFSET 均为
   0x80000000。不符就拒绝加载。
2. BEGIN：调用方给出三个槽（zImage、/sys/firmware/fdt 读出的原厂 FDT、initramfs）的大小和
   SHA256。zImage 必须是构建时钉住的那一个，否则不分配任何页。暂存页避开 zImage 加载窗口
   0x80000000–0x88000000 和持久区段；被避开的页先留在手里，之后一起释放，防止分配器重复给出。
3. PUT：按顺序写入，每次最多 64 KiB。用户缓冲出错会丢弃全部暂存内容。
4. SEAL：依次核对哈希、钉住的内核、原厂 FDT（Zelda，且 memreserve 都在实测排除表内）。
   然后给出布局、改写 FDT、核对 /memory，再校验计划。最后确认没有任何暂存页落在目的地址上
   （复制阶段要一边读源页一边写目的地址）。任何一步失败都丢弃全部暂存内容，
   并在 STATUS 的 seal_stage/seal_result 里记下失败的是哪一步。
5. 最后建出跳转阶段要走的复制表并审一遍（copylist.c）。这里没有任何代码会走它；
   这是离开 Linux 之前最后一件能检查的事，所以现在就在设备上检查，而不是以后假定。
6. STATUS 只报告数据，不给出物理地址；execute_available 恒为 0。

构建：`ports/koa2/build-handoff-module.sh KERNEL_SRC KERNEL_OUT PAYLOAD_OUT`。
它从 PAYLOAD_OUT 生成 target-lock.h（钉住 zImage 大小、哈希、占用和 bootargs），
并在导出前拒绝三种情况：需要 GOT 的模块、导入了构建树没有导出的符号、写协处理器寄存器
（mcr/mcrr）或导入 cpu_down、restart、__dump_lastk_to_mmc、kallsyms_lookup_name 等符号。
koa2-stage-client 是配套的用户态程序（ARM 静态版），只做 BEGIN/PUT/SEAL/STATUS 然后关闭。

测试 koa2-stage 在主机上编译 stage.c 本身，内核接口由 boot/koa2/tests/kshim 替代，
用 ASan/UBSan 运行。覆盖的情况：
- 身份门槛和打开规则；
- BEGIN/PUT 的每种拒绝，包括分配器耗尽和用户缓冲出错；
- SEAL 的每个失败阶段；
- 加载窗口内的页被避开，关闭后页数归零。
传入实机运行时 DTB 时，49280 字节改写为 47972 字节，计划校验通过。

## 复制表（copylist.c）

跳转阶段关掉 MMU 之后不能用分配器、页表和计划，只能走一张表。表的形状和 kexec、PW3 的
一样：若干页 32 位带标记的表项，全部是物理地址。

| 标记 | 含义 |
|---|---|
| KOA2_COPY_DEST | 页对齐的目的地址，后面的源页依次复制到这里 |
| KOA2_COPY_SOURCE | 把这一页复制到当前目的地址 |
| KOA2_COPY_LINK | 下一张表页的物理地址 |
| KOA2_COPY_DONE | 表结束 |

koa2_copylist_audit 是独立的第二个读者：它按写下的字逐项核对，只跟随自己验证过的链接，
并拒绝以下情况：
- 任何源页或表页落在某个目的地址范围内（否则复制顺序会决定结果，走表的代码还可能覆盖
  自己正在读的表）；
- 任何源页或表页落在跳转后仍要存在的区段里；
- 表项与计划不符、链接指向不属于这张表的页、终止符之后还有内容、有表页永远走不到；
- 同一页在一个段里出现两次，地址不对齐或为 0。

测试 koa2-copylist 不只检查写出来的字节，还按跳转阶段的方式走一遍这张表（在一块代替
物理内存的数组上），再核对每个载荷字节是否都到了该去的地方。链接、越界、重叠和被改动的
表项都有用例。写表时的一个计数错误就是这个测试发现的：它把含链接的字数和不含链接的项数
相比。

## 原厂内核的版本检查（module-crcs.py）

原厂内核开了 CONFIG_MODVERSIONS，没开模块签名，也没开强制加载。Linux 4.1 的加载器
（kernel/module.c）只看两件事：
- vermagic 在第一个空格之后的部分必须相同（模块带 CRC 时，same_magic 跳过 release），
  所以 `4.1.15-lab126-koa2-headless-smoke` 与 `4.1.15-lab126` 的差别本身不会被拒；
- module_layout 和每个导入符号的 CRC 必须与内核导出的一致；模块的 __versions 里
  没有某个导入符号也会被拒。

原厂内核的 CRC 在设备上读不到（它们是绝对符号，/proc/kallsyms 不列），但每个原厂模块
的 __versions 都记着它链接时用到的符号 CRC。ports/koa2/module-crcs.py 把这些合起来，
和交接模块的 __versions 逐项对比；原厂模块彼此不一致、vermagic 不止一种、找不到模块、
__versions 长度不对或不是 ARM 文件，都直接拒绝。--symvers 另报告构建树与原厂在共同符号上
是否一致，可以看出改用这棵树重编能否行得通。测试 koa2-module-crcs 用逐字节拼出的模块覆盖
这些规则。

为了让每个导入都能核对，stage.c 不再用原厂模块没用过的四个符号：__vmalloc 换成 vzalloc，
ulong 参数换成 uint，page_to_phys/PHYS_OFFSET（ARM_PATCH_PHYS_VIRT 下要导入
__pv_phys_pfn_offset）换成 virt_to_phys(page_address())，在全部是 lowmem 的 KOA2 上二者相同。
用 headless-smoke 自己的 97 个模块当作"原厂"自检：31 项全部匹配，构建树与这些模块的 1037 个共同符号全部一致。
这只说明工具和模块本身自洽，不说明原厂内核会接受。

Boot Preflight v2（ports/koa2/preflight）会把设备 /lib/modules/$(uname -r) 下的原厂 .ko
原样复制出来（只读，不加载）。拿到之后：
`KOA2_STOCK_MODULES=<目录> ports/koa2/build-handoff-module.sh ...` 会在导出前做这项检查，
有任何一项不匹配就拒绝导出。

2026-11-21 实机证据：本次暂存模块成功加载、封存并卸载；已回收 63 个原厂模块，离线 CRC 核对尚未运行。这证明原厂加载器接受了本次模块，不代表所有模块或所有内核配置兼容。报告入口见[当前进度](../../ports/koa2/STATUS.md)。

## 跳转蹦床（jump.S）

关掉 MMU、走复制表、进新内核。**没有编进暂存模块**，模块也没有任何办法到达它。
只在主机上审过，并在 QEMU 上以裸机方式跑过。

KOA2 这里比 PW3 简单一处：PAGE_OFFSET 和 PHYS_OFFSET 都是 0x80000000，lowmem 本来就是
恒等映射，所以蹦床从自己所在页的物理地址进入时，虚拟地址和物理地址相同，清掉 M 位不会
让指令流跑到别处。因此**不需要私有页表**（PW3 需要，它是 3G/1G）。运行时 DTB 里也没有
外部 L2 控制器（A7 的 L2 在核内），CONFIG_ARM_KERNMEM_PERMS 没开，lowmem 页可执行。

调用方必须先做好、蹦床不替它检查的事：
- 只剩一个核（次核用原厂 cpu_down 下线）；
- SVC 模式、ARM 状态、IRQ 和 FIQ 已屏蔽；
- 所有 DMA 主设备已静止（handoff-quiesce.md）；
- 缓存已清回内存并关闭，TTBCR 为 0；
- 蹦床已复制进交接自己拥有的一页，并从那一页的物理地址进入；
- 复制表已被 koa2_copylist_audit 接受。

蹦床本身：无栈、无外部调用、无字面量池、除自己的参数块外不写任何数据。objdump 核对过
它没有重定位、没有 `ldr rX, [pc, ...]`、不碰 sp、没有 bl/blx，所以可以从复制过去的页上运行。
KJ_BUDGET 要填审计得出的表项数：审计接受过的表走不到这个上限，而被改坏的表会停下而不是空转。

拒绝时记下状态码然后停住。关掉缓存之后已经没有回头路，所以没有"返回调用者"这条路径。
拒绝分两类：第一次读表项之前就能发现的（缓存还开着、链接或源页越界），这时什么都没复制；
只能在走到表中间才发现的（预算用尽、结束时页数不符），这时已经复制的页就留在那里。
jump-abi.h 和测试里都按这个分类记着。

### 裸机测试（tests/baremetal，koa2-jump-baremetal）

主机测试到不了的就是这部分：清 M 位之后从同一个物理地址继续执行、用 PC 找到参数块、
以及跳转时的寄存器约定。所以用真正的 jump.S 和真正的复制表代码，在 QEMU 的
Cortex-A7 上以裸机方式跑：

- `pass`：表被走完，替身内核被进入，并自己核对 r0=0、r1=机型号、r2 指向给它的 FDT
  （只有全对才打印 KO!）；
- `caches_on`、`bad_link`、`bad_source`、`short_count`、`budget`：五种拒绝，
  状态码和"有没有复制过页"都从 QEMU monitor 里读蹦床自己的参数块来核对。

缺交叉编译器或 QEMU 时这个测试自己跳过。它只说明这段代码在 ARMv7 上按设计运行，
不说明 i.MX7D 的外设在真机上的行为。

## 执行模块（exec.c，koa2_handoff_exec.ko）

阶段 F 从这里开始，但这一批不跳转。执行模块和暂存模块是同一套源码：`stage.c` 在
`KOA2_EXEC_BUILD` 下在末尾并入 `exec.c`，所以它复用同一个 store、BEGIN/PUT/SEAL 和
审计过的复制表，设备名换成 `koa2-handoff-exec`。构建由 `Kbuild` 的 `KOA2_EXEC` 分支
决定：执行构建额外链接 `jump.S`（真正的蹦床，不是替身），暂存构建一个字节都不变。

它只多一个只读 ioctl `K2S_EXEC_DRYRUN`（暂存构建收到它返回 `-ENOTTY`）。空跑在封存
之后做五件事，全部可逆：用 `cpu_online` 自己数在线核（不用 `num_online_cpus`，那会引入
原厂核对不了的 `__sw_hweight`）后调 `koa2_quiesce_verify`；重审复制表，表项数必须与封存
时一致；在自有的一页里装好蹦床，用 `koa2_jump_fill`（`jump-params.c`，与裸机测试同一份）
填参数块，再把这页立即交回；屏蔽 IRQ 和 FIQ，只读 CPSR/SCTLR/TTBCR，随即解除；然后停在
关缓存（`KOA2_Q_CACHES`，不可逆点）之前，把一切放回。看门狗不碰——喂狗属于不可逆点，空跑
不到那里，报告记 `watchdog_step=deferred`。`execute_available` 恒为 0，什么都没复制到目的
地址，没停任何设备，没下核，更没有跳转。

构建门禁 `build-exec-module.sh` 守住“不会跳”：Gate 2 用反汇编核对每条 `mcr`/`mcrr` 的地址
都落在 `koa2_jump_start..koa2_jump_end` 内，并且没有任何分支或重定位指进蹦床；禁止导入
`cpu_down`、各种 restart、`kallsyms_lookup_name`。Gate 3 在这里是强制的——执行模块比暂存
模块多了 `cpu_online_mask` 等导入，不给原厂模块就拒绝出包，核对 CRC 为 `passed: true` 才放行。
主机测试 `koa2-exec` 用 kshim 跑 `exec.c` 本身（每种拒绝、屏蔽后恢复、控制页交回、参数字段），
`koa2-exec-wiring` 从源码侧守住上面这些不变量，`koa2-exec-dryrun` 用模拟设备跑设备脚本
`exec-dryrun.sh`（成功按逆序恢复，每步失败各自回滚）。

## 静止顺序（quiesce.c）

把 handoff-quiesce.md 第四节的顺序写成一张表，每一步是一个 run 和（在不可逆点之前）
一个 undo。每一步对硬件做什么要上机才能验；这里能验的是设备上不敢试的那件事：
**第 6 步失败时，原厂系统还在原样吗？**

十步，依次是：校验、EPDC/ePXP、Wi-Fi(usdhc1)、USB gadget、SDMA、eMMC 静止、
次核 cpu_down(1)、屏蔽 IRQ/FIQ、喂一次看门狗、关缓存。最后一步是**不可逆点**：
缓存一关就没有可以回去的原厂内核了，之后只有新内核或看门狗两条路。

koa2_quiesce_check 在任何一步运行之前就拒绝这两种表：
- 不可逆点之前的步骤没有 undo（会把原厂系统改了却回不去）；
- 不可逆点之后的步骤有 undo（宣称一条不存在的退路）。

任何一步失败时，已完成的步骤按逆序回滚，返回那一步自己的错误码；某个 undo 自己失败会
被记下来，但其余的仍然继续，不能因为一样东西要不回来就把其它的都丢在半路。

测试 koa2-quiesce 对**每一步**都试一遍失败，核对回滚的顺序与次数、undo 失败的记录，
以及上面两种表被拒绝。这些都不碰硬件。

### 谁来停设备（2026-11-21 改了设计）

原本打算让模块自己停。两个原因都不成立：**能停设备的符号基本核对不了**
（`clk_disable_unprepare`、`dmaengine_terminate_all`、`mmc_suspend_host` 等构建树不导出；
`lock_fb_info` 的 CRC 与原厂不一致，碰 framebuffer 就会被拒绝加载），
而且**停下来的设备寄存器读不得**——时钟一 gate，i.MX7 上读外设寄存器会挂总线，
所以"读引擎状态确认它空了"这种检查恰恰会在静止成功时把设备挂死。

原方案由用户空间卸载驱动并通过 `/sys/.../cpu1/online` 下线次核。真机测试时 EPDC
仍被占用，USB 和触摸未能原地恢复。EPDC 成功卸载后才能依赖 remove 路径释放 DMA
帧缓冲。eMMC 承载根文件系统，仍须处理同步与原始挂载状态。

模块这一侧只检查结果（`quiesce-verify.c`）：一个核在线、四个驱动都不在、根文件系统只读、
看门狗喂过不超过 10 秒，**不读任何外设寄存器**。这些证据比读引擎状态弱，
注释里写明了，并解释了"引擎是被动的、客户停了就没有流量"是论证而不是测量。
测试 `koa2-quiesce-verify` 专门盯一件事：不能把没静止的机器判成静止的，
包括"检查本身没看全"这种假通过。

`koa2-quiesce-dryrun` 用模拟设备验证只读预检采集状态时保持设备原状。详见
[ports/koa2/handoff-quiesce.md](../../ports/koa2/handoff-quiesce.md) 第六节。
