# device/amazon/koa2

Kindle Oasis 2（KOA2 / i.MX7D / Cortex-A7 / 固件 5.16.2.1.1）的硬件契约与 AOSP 8.1 设备骨架。
当前仅用于主机侧契约校验与后续阶段 H 准备，尚未构建完整 Android 镜像。

| 子系统 | 源码依据 | 契约要点 |
|---|---|---|
| 显示（EPDC v2） | `epdc/EpdcAbi.h` | 1264×1680，8 bpp，虚拟宽度/stride = 1280 |
| 电池（MAX77796） | `drivers/power/max77696-battery.c`、`hardware/PowerContract.h` | `voltage_now` 为 mV、`current_now` 为 mA、`charge_now` 为 mAh（均需 ×1000 给 `healthd`）；`temp` 为 0.1 °C；`capacity` 未限幅（实测见 103），需钳位到 `[0, 100]` |
| 前光 | `drivers/video/backlight/max77696-bl.c` | `/sys/class/backlight/max77796-bl`，`max_brightness=4095`（12-bit） |
| 实体翻页键 | `arch/arm/boot/dts/imx7d-zelda.dts:56-75`、`gpio-keys.kl` | `page-forward` = `KEY_PAGEUP`（104），`page-back` = `KEY_PAGEDOWN`（109） |
| 磁吸保护套 | `drivers/input/misc/hall/bu520.c` | 标准 `EV_SW` / `SW_LID`（0=开盖，1=合盖） |
| 架构骨架 | `BoardConfig.mk` | `armv7-a-neon` / `cortex-a7` / `imx7d`，32-bit Binder，文件镜像不写分区 |
