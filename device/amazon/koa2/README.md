# device/amazon/koa2

Kindle Oasis 2（KOA2 / i.MX7D / Cortex-A7 / 固件 5.16.2.1.1）的硬件契约与 AOSP 8.1 设备骨架。
当前仅用于主机侧契约校验与后续阶段 H 准备，尚未构建完整 Android 镜像。

| 子系统 | 源码依据 | 契约要点 |
|---|---|---|
| 显示（EPDC v2） | `hardware/EpdcContract.h` | 1264×1680，8 bpp，虚拟宽度/stride = 1280，硬拒绝夜间波形（`GCK16`/`GLKW16`） |
| 触摸屏（CYTTSP5） | `cyttsp5_mt.idc` | `cyttsp5_mt` 内置触摸屏，随方向旋转 |
| 加速度与双光感 | `hardware/SensorContract.h` | `bosch_accel@18`（运行时 DTB `bma,place=1`）、4 向迟滞旋转、双 `max44009`（`0x4a`/`0x4b`）取高值防遮挡 |
| 电池（MAX77796） | `drivers/power/max77696-battery.c`、`hardware/PowerContract.h` | `voltage_now` 为 mV、`current_now` 为 mA、`charge_now` 为 mAh（均需 ×1000 给 `healthd`）；`temp` 为 0.1 °C；`capacity` 未限幅（实测见 103），需钳位到 `[0, 100]` |
| 前光 | `drivers/video/backlight/max77696-bl.c` | `/sys/class/backlight/max77796-bl`，`max_brightness=4095`（12-bit） |
| 实体翻页键 | `arch/arm/boot/dts/imx7d-zelda.dts:56-75`、`gpio-keys.kl` | `page-forward` = `KEY_PAGEUP`（104），`page-back` = `KEY_PAGEDOWN`（109） |
| 磁吸保护套 | `drivers/input/misc/hall/bu520.c` | 标准 `EV_SW` / `SW_LID`（0=开盖，1=合盖） |
| 架构骨架 | `BoardConfig.mk` | `armv7-a-neon` / `cortex-a7` / `imx7d`，32-bit Binder，文件镜像不写分区 |
| 根文件系统骨架 | `rootdir/{init,ueventd}.koa2.rc`、`fstab.koa2` | 300 PPI、`max77696-bl` 权限、纯内存验证挂载（不写原厂分区） |
| 产品与 HIDL 清单 | `koa2.mk`、`AndroidProducts.mk`、`manifest.xml`、`system.prop` | AOSP 8.1 产品定义、`sensors@1.0`/`health@1.0`/`light@2.0`、`low_ram` + `-Xnorelocate`（512 MiB 优化） |
| SELinux 骨架 | `sepolicy/{file_contexts,epdc.te}` | 仅授权 `/dev/graphics/fb0` 与 `max77696-bl`，不开放任何裸块设备 |
