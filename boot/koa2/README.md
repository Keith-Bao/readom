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
