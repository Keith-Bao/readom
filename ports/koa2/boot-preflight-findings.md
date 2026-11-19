# 只读启动预检结果（2026-11-19）

来源：用户在 Kindle 上手动运行 Boot Preflight v1（运行号 20261119-044638-4937）。
原始文件（运行时 DTB、bootargs、上次内核日志）只保存在设备和构建主机
`~/koa2-evidence/` 内，含序列号等设备标识，不进仓库。下面只记录与交接相关、
不含个人标识的数值。原厂内核 4.1.15-lab126，固件 5.16.2.1.1。

## 物理内存

| 范围 | 来源 | 含义 |
|---|---|---|
| 0x80000000–0x9bffffff | /proc/iomem System RAM | 可用 RAM 第一段（448 MiB） |
| 0x9c000000–0x9c4fffff | DT `reserved-memory/epdc_reserved`，no-map | EPDC 工作缓冲 5 MiB，iomem 里是空洞 |
| 0x9c500000–0x9fffffff | /proc/iomem System RAM | 可用 RAM 第二段（59 MiB） |
| 0x80008000–0x80c0facb | iomem Kernel code/data | 正在运行的原厂内核 |
| 0x9be00000–0x9be7ffff | printk.c `LOG_GOAL_LOCATION` ×2 | Lab126 printk 缓冲及前次日志备份（源码常量，运行日志印证 `/proc/printkbuf` 可读） |
| 0x9bf00000–0x9bffffff | 内核日志 `ramoops: attached 0x100000@0x9bf00000` | ramoops，由 mach-imx7d.c 运行时给出，不在 DT 里 |
| 0x9d681000 +0xc000 | FDT header memreserve #1 | **FDT 本身**：U-Boot `image_setup_libfdt` → `fdt_shrink_to_minimum` 为 blob 加的保留，大小按页对齐；随后 `FDT_RAMDISK_OVERHEAD`（0x80）使 totalsize 成为 0xc080，所以排除表多盖一页（到 0x9d68e000） |
| 0x9ffc0000–0x9fffffff | 内核配置 `CONFIG_FALCON_STORAGE_BIOS_ADDR`/`FALCON_BIOS_ADDR`，fbios.c `memblock_reserve` | **Falcon BIOS**（存储 BIOS 64 KiB + BIOS 192 KiB）。在 iomem 里算 System RAM，但原厂对 eMMC 的访问（falcon_blk）在这里执行；探针抓到的原厂 kernel.config 与本机构建取值相同 |
| 0x9d691000 +0x55aa00 | FDT header memreserve #2 | 原厂 initrd（与 /chosen initrd-start/end 一致），启动后被释放 |

DT `/memory` 为 0x80000000 + 0x20000000，与编译 DTB 相同；真实可用范围要扣掉上表的保留区。
lowmem 0x80000000–0xa0000000（512 MiB），vmalloc 0xa0800000–0xff000000。
日志的 lowmem 虚拟起点是 0x80000000，说明 PAGE_OFFSET=0x80000000
（2G/2G）且等于 PHYS_OFFSET，lowmem 为恒等映射，与 Oasis 1 的分析同类。本机构建的 zelda 配置也是
`CONFIG_VMSPLIT_2G=y`、`CONFIG_PAGE_OFFSET=0x80000000`、`CONFIG_HIGHMEM=y`，与日志一致。交接设计据此复核，未上机验证。

## 运行时 DTB 与编译 DTB 的差别

- 运行时 49280 字节、288 节点、1519 属性、2 个 header reserve；编译 DTB 0 个 reserve、无 bootargs。
- 运行时多出引导程序按 IDME 填入的节点（board_id、bootcount、bootmode、bt_mac_addr、
  dev_flags、device_type_id、alscal 等），属于每台设备自己的数据。
- 传感器组合不同：运行时有 prox/touch 的 pinctrl 组，编译 DTB 有 `smtc_sar@28`、`ti_als@44`。
  说明 zelda 一个 dts 对应多种板，实机由引导程序修正。
- 结论：交接只能以**这台设备的运行时 FDT** 为基础（原样传递或在设备上派生），不能用编译 DTB 替换。
- `contract.c` 对运行时 DTB：结构检查通过；ASan/UBSan 下 20000 次变异测试通过。`safe_to_boot` 仍为 false。

## CPU 与符号

- CPU 0-1 online，cpu1 可热插拔接口存在。
- kallsyms 可读：cpu_down、cpu_up、stop_machine、kallsyms_lookup_name、imx_cpu_die、
  imx_cpu_kill、imx_gpcv2_set_core1_pdn_pup_by_software 均导出；没有 machine_kexec（无 kexec）。
- tainted=4609 = P(1) + W(512) + O(4096)：本次启动中原厂内核出现过一次 WARN，来源未查。

## 日志通路

- `/proc/printkbuf` 读到 256 KiB 前次日志，能用作新内核失败后的日志来源（前提是新内核不覆盖
  0x9be00000 起的 512 KiB，且冷/热重启行为另行确认）。
- 前次日志里两次出现 `__dump_lastk_to_mmc`（battery cut 前），即原厂会**直接写 MMC 原始扇区**。
  这条路径在实机上确实会触发；新内核/交接期间不得让它运行，交接前必须确保不会被调用。
- `/sys/fs/pstore` 为空；ramoops 区有，但当前没有可读记录。

## 对下一步的约束

1. 交接载荷与新内核的目的地必须避开上表所有保留区。PW3 的"目的地留在低 128 MiB
   （0x80000000–0x88000000）"规则若沿用到 KOA2，能把它们全部排除（KOA2 尚未定这条规则）；但正在运行的原厂内核位于
   0x80008000–0x80c0facb，载荷目的地与当前内核重叠的部分只能在最后一步切换后写入。
2. 需要上机确认的还有：热重启后 printk 备份是否保留、WARN 来源、Falcon BIOS 工作区（日志 "BIOS require 4587520 bytes"，memblock 动态分配）的实际地址。
3. 仍没有任何新内核上机，也没有交接模块。

## 原厂会写 eMMC 原始扇区的路径（源码审计，2026-11-19）

`dump_lastk_to_mmc()`（kernel/printk/printk.c）把 256 KiB printk 缓冲经 `__dump_lastk_to_mmc()`
（drivers/falcon/block.c）通过 Falcon BIOS 服务 `bios_svc(0x13, RT_WRITE, …)` 写到 eMMC 用户区
扇区 0x9a400 起 512 个扇区（字节偏移 0x13480000–0x134bffff，`PERSISTENT_KEYS_OFFSET` 之前）。
这个位置在 mmcblk2p1 起点（扇区 575488 = 0x8c800）之前，不在任何分区里。调用方只有三个：

| 调用方 | 触发条件 |
|---|---|
| drivers/input/misc/max77696-key.c `max77696_key_mrwrn_isr` | 长按电源键约 15 秒的电池切断预警（实机日志里的两次就是它） |
| drivers/input/misc/max77696-key.c `max77696_key_sw_key_event` | PMIC 软件按键事件 |
| drivers/watchdog/imx2_wdt.c `imx2_restart_handler`（priority 128） | **每一次**经看门狗的重启，包括正常 `reboot` |

结论：

- 原厂内核在**正常重启时也会写这块扇区**，这是原厂的既有行为，不是交接引入的。
- 交接时原厂内核不再运行，这三条路径都不会走到；风险在两处：
  1. 交接本身若经由 `kernel_restart()`/`machine_restart()`，会先进 imx2 的 restart handler 写 eMMC。
     交接必须像 PW3 那样自己停 CPU、关中断后跳转，**不能借用重启路径**。
  2. 新内核（headless-smoke 及以后）保留了 `CONFIG_LAB126_PRINTK_BUFFER`、`FALCON_BLK`、
     `IMX2_WDT`、`INPUT_MAX77696_KEY`，所以新内核自己重启或长按电源键时同样会写这块扇区。
     这块扇区原厂本来就在写，写的是日志；但新内核若改了 `LOG_GOAL_LOCATION` 的内容格式或 Falcon BIOS
     被覆盖后再调用，后果未知。所以 Falcon BIOS 区必须保留不动（已加进排除表），并且新内核在 Falcon BIOS
     可用性验证之前，应当让 `dump_lastk_to_mmc` 不发起写（例如实验内核关掉 `CONFIG_FALCON_BLK`，原厂 eMMC
     访问改走标准 sdhci 驱动——这需要另行核实 mmcblk2 能否由 usdhc@30b60000 标准驱动直接访问）。
- 这一节只是源码审计；没有在设备上触发或观察过任何写入。
