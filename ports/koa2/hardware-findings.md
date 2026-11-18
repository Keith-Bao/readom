# Oasis 2 首次实机采集

采集完成标志：`v1 complete`。原始报告及 kernel.config 仅保存在本地
`F:\Kindle\koa2-evidence`，此文档只记录移植相关事实。

| 项目 | 实机结果 |
|---|---|
| 固件 | 5.16.2.1.1，USB 版本文件构建号 409745 002 |
| 内核 | 4.1.15-lab126，#1 SMP PREEMPT Tue Aug 1 11:50:12 UTC 2023 |
| 编译器 | GCC 4.9.1 |
| DT compatible | fsl,imx7d-zelda / fsl,imx7d |
| CPU | 两个 Cortex-A7（CPU part 0xc07），ARMv7、NEON |
| MemTotal | 493748 kB |
| 虚拟地址划分 | VMSPLIT_2G，PAGE_OFFSET=0x80000000，非 LPAE |
| 系统 RAM 范围 | 0x80000000–0x9bffffff；0x9c500000–0x9fffffff |
| 原厂根分区 | /dev/mmcblk2p5；用户存储为 mmcblk2p7 |
| 显示 | mxc_epdc_fb，模块 mxc_epdc_v2_fb，1264×1680，8 bpp，rotate=0 |
| framebuffer 虚拟大小 | 1280×3584，不能将可见宽度直接当作 stride |
| 前光 | /sys/class/backlight/max77796-bl，max_brightness=4095 |
| 电池/充电 | max77796-battery / max77796-charger |
| Wi-Fi | bcmdhd 模块，SDIO；配置指向 brcm_4343w 固件 |
| 触摸 | cyttsp5_mt / cyttsp5_i2c |
| 其他输入 | gpio-keys、bma2x2、bma_interrupt、hall_sensor_disp/usb、max44009_als/als1 |
| USB | configfs/libcomposite + g_mass_storage |

## 内核配置初查

原厂 `CONFIG_ANDROID`、`CONFIG_SECCOMP`、`CONFIG_NAMESPACES`、`CONFIG_AUDIT`
均关闭；不能直接用原厂配置运行本项目的 Android 8.1。
CGROUPS、DEVTMPFS、SWAP、ZRAM、ZSMALLOC、IKCONFIG_PROC 已启用。
这些只是原厂配置事实，尚未证明相关 Android 接口实现是否满足 AOSP 8.1。

不能沿用 PW3 的 3.0.35 配置片段作为 KOA2 的构建配置：它带有 MAX77696、
CYTTSP4、ath6kl、ARC USB 和 PW3 专有补丁开关。需要逐项审计 4.1 源码。
也不能直接加载其他内核版本的原厂 bcmdhd 模块。

电池节点报告 capacity=103、voltage_now=4118、current_now=-147、temp=271。
这些原始值显示需要在源码里核实量纲和有效范围；HAL 不能默认已经是标准
power_supply ABI 的微伏/微安，不能用这次快照推断续航或电池健康。

## 修正采集器

v1 报告已包含分区列表，但分区属性循环和 mount 过滤器写死了 mmcblk0，
所以 KOA2 的分区 start/size/ro 和 mount 条目缺失。现改为匹配所有 mmcblk
设备；需要再运行一次以获得这些字段。报告写入同一目录并覆盖上一份，
首次报告已备份到电脑。

## 官方源码

来源：[Amazon Source Code Notice](https://digprjsurvey.amazon.com/csad/help/node/200203720)。
目标归档：`Kindle_src_5.16.2.1.1_4097450002.tar.gz`。
下载地址：`https://kindledownloads.s3.amazonaws.com/Kindle_src_5.16.2.1.1_4097450002.tar.gz`。
HTTP 返回长度 356269633 字节。下载后必须核验实际归档内容和 SHA-256，
不能因 URL 版本匹配就宣称源码能还原实机内核。

归档现已完整下载，长度吻合；归档及内层内核包 SHA-256 已记入
`source-lock.json`。已提取并检查 `imx_v7_zelda_defconfig`、
`arch/arm/boot/dts/imx7d-zelda.dts` 和 `drivers/staging/android/Kconfig`。
defconfig 包含 VMSPLIT_2G、BCMDHD SDIO、MAX77796；Android Kconfig
已有 ashmem 和 low-memory-killer 的开关，但仍需审计实现版本与构建结果。

下一步：从官方归档定位 4.1.15 源码、zelda DT 和 defconfig；审计 Android
接口与 EPDC v2 ABI；在独立 Linux 构建环境里先复现原厂内核构建。
当前尚无可安装的 KOA2 Android 镜像，也未对实机执行启动交接。
