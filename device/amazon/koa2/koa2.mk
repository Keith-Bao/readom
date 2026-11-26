# SPDX-License-Identifier: Apache-2.0
# Kindle Oasis 2 (KOA2 / i.MX7D / 512 MiB DRAM) AOSP 8.1 product definition.
# Host-prepared tree; not yet built into a bootable AOSP image.

$(call inherit-product, $(SRC_TARGET_DIR)/product/core.mk)

PRODUCT_NAME := koa2
PRODUCT_DEVICE := koa2
PRODUCT_BRAND := Amazon
PRODUCT_MODEL := Kindle Oasis 2
PRODUCT_MANUFACTURER := Amazon
PRODUCT_CHARACTERISTICS := nosdcard,tablet

PRODUCT_AAPT_CONFIG := normal hdpi xhdpi
PRODUCT_AAPT_PREF_CONFIG := xhdpi

PRODUCT_COPY_FILES += \
    device/amazon/koa2/cyttsp5_mt.idc:system/usr/idc/cyttsp5_mt.idc \
    device/amazon/koa2/gpio-keys.kl:system/usr/keylayout/gpio-keys.kl \
    device/amazon/koa2/rootdir/init.koa2.rc:root/init.koa2.rc \
    device/amazon/koa2/rootdir/ueventd.koa2.rc:root/ueventd.koa2.rc \
    device/amazon/koa2/rootdir/fstab.koa2:root/fstab.koa2 \
    device/amazon/koa2/rootdir/init.koa2.usb.rc:root/init.koa2.usb.rc \
    device/amazon/koa2/wifi/wpa_supplicant_overlay.conf:system/etc/wifi/wpa_supplicant_overlay.conf

PRODUCT_PACKAGES += \
    lights.koa2 \
    power.koa2 \
    android.hardware.light@2.0-service \
    android.hardware.light@2.0-impl \
    android.hardware.power@1.0-service \
    android.hardware.power@1.0-impl \
    android.hardware.health@1.0-service \
    android.hardware.health@1.0-impl \
    android.hardware.keymaster@3.0-service \
    android.hardware.keymaster@3.0-impl \
    android.hardware.graphics.allocator@2.0-service \
    android.hardware.graphics.allocator@2.0-impl \
    android.hardware.graphics.mapper@2.0-impl \
    android.hardware.audio@2.0-service \
    android.hardware.audio.effect@2.0-impl \
    audio.primary.default \
    audio.r_submix.default

PRODUCT_PROPERTY_OVERRIDES += \
    ro.config.low_ram=true \
    ro.sf.lcd_density=300 \
    ro.hardware.egl=swiftshader
