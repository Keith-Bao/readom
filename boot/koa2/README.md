# KOA2 交接契约开发

当前只有只读 DTB 结构/机型校验与内存区间检查，没有模块加载、内存复制、
CPU 下线或内核跳转。`safe_to_boot` 固定为 false。

contract.c 拒绝截断、重叠块、无终止 reserve map、深度过大、非法 token、
错误 Zelda compatible、溢出的内存区间及跨越允许 RAM 边界的载荷。
它不是完整 libfdt 替代品，也不验证面板校准、所有 DT 属性语义或允许跳转。
只有在全部原厂专属数据、保留内存和运行环境契约核实后，才能加入后端。

主机测试已登记 tools/run-tests.py，启用 ASan/UBSan；包括 20000 次确定性
变异输入。另以 ARM 静态二进制在 qemu-arm 上运行同样测试和实际构建 DTB。
实际 DTB：50984 字节、260 节点、1475 属性、0 个 header reserve 条目。
0 条目不等于没有保留内存：内核启动过程还会动态预留 RAM。

当前编译 DTB /memory 为 0x80000000 + 0x20000000，而实机 /proc/iomem
另有不可用空洞；/chosen 没有 bootargs。不能以此 DTB 直接替换原厂运行时数据。

## 重复验证

Linux 下以 gcc -std=c11 -Wall -Wextra -Werror -O1 -g
-fsanitize=address,undefined -fno-sanitize-recover=all 编译 contract.c 与
test-contract.c，传入真实 DTB 作为可选参数。ARM 静态编译使用
arm-linux-gnueabi-gcc -static；qemu-arm 只验证用户空间算法，不证明 i.MX7 外设行为。

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
   initramfs。原厂的 FDT/initrd 条目会让内核预留已失效的内存，还会让 arm_memblock_init
   以"与已用区域重叠"为由丢弃 initramfs。

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

以上都只是源码和主机层面的结论，没有在设备上验证。还没写的：
- 交接模块本身，包括暂存、静止、复制和跳转；
- 在交接当次读取 EPDC 帧缓冲地址。
