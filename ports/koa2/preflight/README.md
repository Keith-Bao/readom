# 只读启动信息采集

部署至 Kindle extensions/readom-koa2-boot-preflight；菜单为
KUAL → Readom KOA2 Boot Preflight → Collect read-only boot report。
仅由用户在方便时启动；这不是新内核启动实验。

输出在 Kindle 的 koa2-boot-preflight/<UTC时间-PID>/，LAST_RUN.txt 指向
最新记录；complete.txt 表示采集结束。包括运行时 FDT、CPU 状态、选定符号、
内存范围、上次内核日志。只读取设备，不联网、不加载模块、不改 CPU/挂载/硬件设置。
所有写入限定在报告目录。

原始 DTB 和启动参数可能含设备标识，只保留在用户设备/电脑，不能提交公共仓库。
检查器只读原始 DTB，其成功结果不意味着可启动。

inspect-fdt-arm 为自有 C 源码的静态 ARM 构建，来自 boot/koa2/，不存入 Git。
编译命令：arm-linux-gnueabi-gcc -std=c11 -Wall -Wextra -Werror -O2 -static
boot/koa2/contract.c boot/koa2/inspect-fdt.c -o inspect-fdt-arm。
需要 libc6-dev-armel-cross；主机 qemu-arm 实测通过，尚未在 Kindle 执行。
如果二进制在设备上不能运行，采集脚本仍会保存报告和原始 DTB以便电脑分析。
