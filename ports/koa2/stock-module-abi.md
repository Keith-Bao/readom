# 原厂内核的模块 ABI（2026-11-21 实机确认）

English: [stock-module-abi.en.md](stock-module-abi.en.md)。

2026-11-21 的实机暂存测试里 `insmod` 成功（报告见
[reports/2026-11-21-stage-test/report.txt](reports/2026-11-21-stage-test/report.txt)），
所以模块的 31 个导入和 `module_layout` 带的 CRC 就是原厂内核导出的那些。
设备拷回来的 63 个原厂模块在
[materials/2026-11-21/device-evidence/stock-modules](materials/2026-11-21/device-evidence/stock-modules)，
这件事因此是可以被测试守住的事实，不是一次要记住的结果。测试为 `koa2-stock-abi`。

## 原来的预期是错的

之前记的是"最可能 insmod_refused"，理由是模块对 headless-smoke 树编译、CRC 对不上。
实际加载成功。两件事让它成立：

- `same_magic` 在模块带 CRC 时跳过 release 字符串，所以
  `4.1.15-lab126-koa2-headless-smoke` 与 `4.1.15-lab126` 的差别本身不会被拒；
- 这个模块导入的 31 个符号，CRC 恰好与原厂一致。

"恰好"是实话：构建树与原厂在 1129 个共同符号上有 12 个不一致，只是模块一个都没用到。

## 12 个不一致的符号，和各自的原因

| 符号 | 原因 |
|---|---|
| `cfb_copyarea`、`cfb_fillrect`、`cfb_imageblit`、`fb_set_var`、`framebuffer_alloc`、`framebuffer_release`、`lock_fb_info`、`register_framebuffer`、`unregister_framebuffer` | 构建树关掉了 EINK 面板（源码缺口见 [epdc-source-gap.md](epdc-source-gap.md)）。模块一个都没导入 |
| `set_cpus_allowed_ptr`、`sched_setscheduler`、`wake_up_process` | `CONFIG_FALCON_WRAPPER`。`kernel/sched/core.c` 在这个选项下 `#include <linux/falcon_storage.h>`，genksyms 把它算进该文件导出的每个符号的 CRC |

后三个是实验查明的：在构建树里把 `FALCON_WRAPPER` 打开重编，三个 CRC 全部变成原厂的值
（`0xe632f7cc`、`0xd09a0a5e`、`0xd7e0ebd8`），九个 framebuffer 符号不变。

## 因此分两棵树

| 树 | 配置 | 用途 |
|---|---|---|
| `out-module-abi` | headless-smoke 加 `FALCON_WRAPPER=y`（面板仍关） | **模块编译针对的树**。它的 zImage 不是用来启动的 |
| `out-headless-smoke` | 面板关、Falcon 全关 | **要启动的新内核**（载荷），保持不写 eMMC 原始扇区的性质 |

`build-stock-kernel.sh module-abi` 造前者，它只编 vmlinux 和 modules，因为只需要
`Module.symvers`。`build-stage-test-kit.sh` 已按这个分工：模块针对 `out-module-abi` 编，
载荷用 `out-headless-smoke` 的 zImage。

这样做不是为了修现在的问题（31 个导入本来就对得上），而是为了下一阶段：静止顺序要用
`cpu_down`，它在 `kernel/cpu.c`，不受上面两个选项影响，但一旦将来用到
`kernel/sched/core.c` 导出的任何符号，针对这棵树编就已经是对的。

## cpu_down 核对不了

`cpu_down` 没有任何原厂模块导入它，所以它的 CRC 在设备上读不到，**没法在加载前核对**。
两棵配置差别很大的树给出的都是 `0x7cb1ae69`，这让它看起来稳定，但那不是证据。
唯一的检验是在设备上 `insmod`：对不上时内核会干净地拒绝，不会崩。
测试 `koa2-stock-abi` 把这一点写在用例里，免得下一个人以为已经核对过。
