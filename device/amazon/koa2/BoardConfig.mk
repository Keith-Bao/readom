# SPDX-License-Identifier: Apache-2.0
# Kindle Oasis 2 (KOA2 / i.MX7D / Cortex-A7) AOSP 8.1 board skeleton.
# Host-prepared configuration; not yet built into a flashable/bootable AOSP image.

TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_VARIANT := cortex-a7
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_BOARD_PLATFORM := imx7d
TARGET_BOOTLOADER_BOARD_NAME := koa2

TARGET_NO_BOOTLOADER := true
TARGET_NO_KERNEL := true
TARGET_NO_RECOVERY := true
TARGET_USES_64_BIT_BINDER := false

# Software composition over i.MX7D EPDC v2 framebuffer (1264x1680, stride 1280, 8 bpp).
TARGET_USES_HWC2 := false
TARGET_RUNNING_WITHOUT_SYNC_FRAMEWORK := true
USE_OPENGL_RENDERER := true
NUM_FRAMEBUFFER_SURFACE_BUFFERS := 2

BOARD_USES_GENERIC_AUDIO := true
USE_XML_AUDIO_POLICY_CONF := 1
USE_CAMERA_STUB := true

TARGET_USERIMAGES_USE_EXT4 := true
TARGET_COPY_OUT_VENDOR := system/vendor

# File-image budgets (no device partition resize or raw partition writes).
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 536870912
BOARD_USERDATAIMAGE_PARTITION_SIZE := 1879048192
