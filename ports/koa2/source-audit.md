# KOA2 源码兼容性审计（未编译内核）

第二次采集 `v2 complete` 已补齐分区 start/size/ro。原始文件备份在
`F:\Kindle\koa2-evidence\v2`。以下扇区均为 Linux sysfs 的 512 字节单位。

| 分区 | 起始扇区 | 扇区数 |
|---|---:|---:|
| mmcblk2p1 | 575488 | 28672 |
| mmcblk2p2 | 604160 | 28672 |
| mmcblk2p3 | 632832 | 4096 |
| mmcblk2p4 | 636928 | 8192 |
| mmcblk2p5 | 645120 | 1048576 |
| mmcblk2p6 | 1693696 | 524288 |
| mmcblk2p7 | 2217984 | 13051837 |

总盘 15269888 扇区。表只记录布局，不是分区写入方案。现有采集已足够继续
源码审计，不需要再跑采集脚本；完整启动信息若有额外需求，应另行设计采集。

## 已确认的阻塞

1. **SMP 启动交接**：`boot/handoff/stage.c` 在 `CONFIG_SMP` 时直接报错
   `Only UP ARM32 with 4 KiB pages is supported`。实机为 SMP，不能通过
   照抄 profile 或删除这一检查来支持。还要处理次核停止、缓存、DMA 和设备树。
   官方 `arch/arm/kernel/machine_kexec.c` 有 DTB 识别和 CPU hotplug 前置检查，
   可作为审计参考；但实机 CONFIG_KEXEC 关闭，不能直接从用户空间调用 kexec。
2. **启动参数**：本机硬件描述为 Device Tree；现有 handoff core 校验和重写
   ATAGs。需要设计并验证 DTB 交接，不是只替换机器号或内存地址。
3. **Binder**：官方 `drivers/android/binder.c` 使用全局
   `binder_context_mgr_node` 并只注册一个名为 binder 的 miscdevice；
   Kconfig 没有 ANDROID_BINDER_DEVICES。Android 8.1 所需多上下文支持需要
   实现层适配，不能仅在配置中写 binder,hwbinder,vndbinder。
4. **显示 ABI**：官方 `include/uapi/linux/mxcfb.h` 的 mxcfb_update_data
   为 88 字节：dither/quant 在 alt_buffer 前，hist 和两个 timestamp 在末尾。
   PW3 的 EpdcUpdate 为 72 字节且字段顺序不同。ioctl 编码含结构大小，不能混用。
   KOA2 TEMP_USE_AUTO=0x1000，而 PW3 定义为 0x1001。
   新增 `device/amazon/koa2/epdc/EpdcAbi.h` 单独记录 KOA2 接口；尚未接入 HAL。
   上游 PW3 的 GetAndroidInfo 是配套内核补丁接口，不能默认 KOA2 已实现。
   已用 MSVC C++14、/W4 /WX /Zs 检查便携结构定义，大小和字段偏移的
   static_assert 全部通过；Linux ioctl 分支与 ARM 实机行为仍未编译验证。
5. **构建**：本机还没有可用的 WSL Linux 环境及 AOSP 工具链，Windows 的
   MSVC 只能用于便携数据布局语法检查，不能据此宣称 ARM 内核或 Android 构建成功。

## 后续验证顺序

先在 Linux/ARM 交叉编译环境复现 zelda 原厂内核，再逐项移植 Android 内核接口；
单独验证 DTB/SMP 交接和可恢复的启动实验，之后接入 EPDC v2、供电、输入、Wi-Fi。
目前只进行了 USB 存储文件读写和主机源码检查，未加载新内核模块或写入原始分区。
