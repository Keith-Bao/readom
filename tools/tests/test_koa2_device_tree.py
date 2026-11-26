#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Validate device/amazon/koa2 AOSP 8.1 skeleton consistency and safety invariants."""
from pathlib import Path
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
KOA2 = ROOT / 'device/amazon/koa2'

required = [
    'AndroidProducts.mk', 'BoardConfig.mk', 'koa2.mk', 'vendorsetup.sh',
    'manifest.xml', 'system.prop', 'cyttsp5_mt.idc', 'gpio-keys.kl', 'README.md',
    'rootdir/init.koa2.rc', 'rootdir/ueventd.koa2.rc', 'rootdir/fstab.koa2',
    'sepolicy/file_contexts', 'sepolicy/epdc.te', 'sepolicy/genfs_contexts',
    'hardware/PowerContract.h', 'hardware/EpdcContract.h', 'hardware/SensorContract.h',
    'lights/lights.c', 'lights/Android.mk',
    'health/healthd.cpp', 'health/Android.mk',
    'power/power.c', 'power/Android.mk',
    'rootdir/init.koa2.usb.rc', 'wifi/wpa_supplicant_overlay.conf',
]
for rel in required:
    assert (KOA2 / rel).is_file(), f'missing {rel}'

# 1. Every PRODUCT_COPY_FILES source in koa2.mk exists in the repo.
mk = (KOA2 / 'koa2.mk').read_text()
pairs = re.findall(r'(device/amazon/koa2/[^\s:\\]+):[^\s\\]+', mk)
assert len(pairs) >= 6, pairs
for src in pairs:
    assert (ROOT / src).is_file(), f'koa2.mk references missing file: {src}'

# 2. manifest.xml is valid XML and declares core HIDL interfaces including health (sensors deferred).
tree = ET.parse(KOA2 / 'manifest.xml')
root = tree.getroot()
assert root.tag == 'manifest' and root.attrib.get('type') == 'device'
hals = {h.findtext('name') for h in root.findall('hal')}
for wanted in ('android.hardware.graphics.allocator', 'android.hardware.graphics.mapper',
               'android.hardware.health', 'android.hardware.light',
               'android.hardware.keymaster'):
    assert wanted in hals, (wanted, hals)

# 3. BoardConfig.mk / system.prop / ueventd / fstab safety invariants.
board = (KOA2 / 'BoardConfig.mk').read_text()
assert 'TARGET_USES_64_BIT_BINDER := false' in board
assert 'TARGET_ARCH := arm' in board

prop = (KOA2 / 'system.prop').read_text()
assert 'ro.sf.lcd_density=300' in prop
assert '-Xnorelocate' in prop
assert 'ro.config.low_ram=true' in prop

ueventd = (KOA2 / 'rootdir/ueventd.koa2.rc').read_text()
assert '/dev/block/' not in ueventd
assert '/sys/class/backlight/max77796-bl brightness' in ueventd

fstab = (KOA2 / 'rootdir/fstab.koa2').read_text()
assert '/dev/block/' not in fstab
assert 'tmpfs /data' in fstab

se = (KOA2 / 'sepolicy/epdc.te').read_text()
assert 'block_device' not in se

# sysfs is labelled through genfscon, not file_contexts, and grants no raw block.
genfs = (KOA2 / 'sepolicy/genfs_contexts').read_text()
assert 'genfscon sysfs /devices/platform/30400000.aips-bus/306f0000.epdc' in genfs, 'EPDC genfscon missing'
assert 'block_device' not in genfs
# the i2c-attached leaf paths (dynamic i2c-N) stay commented until captured on device.
assert genfs.count('\ngenfscon sysfs ') == 1, 'exactly one active genfscon expected'
# file_contexts carries only real device nodes, no ineffective /sys entries.
fc = (KOA2 / 'sepolicy/file_contexts').read_text()
fc_lines = [l for l in fc.splitlines() if l.strip() and not l.lstrip().startswith('#')]
assert not any(l.startswith('/sys/') for l in fc_lines), 'no active /sys line in file_contexts'
assert any('/dev/graphics/fb0' in l for l in fc_lines)
# every HAL the manifest declares has its service package in koa2.mk.
pkgs = (KOA2 / 'koa2.mk').read_text()
for svc in ('android.hardware.light@2.0-service', 'android.hardware.power@1.0-service',
            'android.hardware.health@1.0-service', 'android.hardware.keymaster@3.0-service',
            'android.hardware.graphics.allocator@2.0-service', 'android.hardware.graphics.mapper@2.0-impl',
            'android.hardware.audio@2.0-service', 'android.hardware.light@2.0-impl',
            'android.hardware.power@1.0-impl', 'android.hardware.health@1.0-impl'):
    assert svc in pkgs, f'{android_svc if False else svc} missing from koa2.mk PRODUCT_PACKAGES'
assert 'android.hardware.sensors' not in hals, 'sensors HAL has no sensors.koa2 module yet'
# wifi.supplicant was dropped from the manifest: no service module in this tree.
# check the declared HAL names, not comment text: wifi.supplicant is no longer served.
assert 'android.hardware.wifi.supplicant' not in hals
# BOARD_HAL_STATIC_LIBRARIES wires healthd to the KOA2 battery adaptation.
assert 'BOARD_HAL_STATIC_LIBRARIES := libhealthd.koa2' in (KOA2 / 'BoardConfig.mk').read_text()
assert 'ro.adb.secure=1' in prop
usb_rc = (KOA2 / 'rootdir/init.koa2.usb.rc').read_text()
assert 'ci_hdrc.0' in usb_rc and '/dev/block/' not in usb_rc
wifi_cfg = (KOA2 / 'wifi/wpa_supplicant_overlay.conf').read_text()
assert 'psk=' not in wifi_cfg and 'ssid=' not in wifi_cfg

print('KOA2 device tree: koa2.mk copy targets, HIDL manifest, 32-bit binder, 300 PPI, and no-raw-block invariants passed')
