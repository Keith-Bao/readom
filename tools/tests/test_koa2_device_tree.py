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
    'sepolicy/file_contexts', 'sepolicy/epdc.te',
    'hardware/PowerContract.h', 'hardware/EpdcContract.h', 'hardware/SensorContract.h',
]
for rel in required:
    assert (KOA2 / rel).is_file(), f'missing {rel}'

# 1. Every PRODUCT_COPY_FILES source in koa2.mk exists in the repo.
mk = (KOA2 / 'koa2.mk').read_text()
pairs = re.findall(r'(device/amazon/koa2/[^\s:\\]+):[^\s\\]+', mk)
assert len(pairs) >= 6, pairs
for src in pairs:
    assert (ROOT / src).is_file(), f'koa2.mk references missing file: {src}'

# 2. manifest.xml is valid XML and declares core HIDL interfaces including sensors & health.
tree = ET.parse(KOA2 / 'manifest.xml')
root = tree.getroot()
assert root.tag == 'manifest' and root.attrib.get('type') == 'device'
hals = {h.findtext('name') for h in root.findall('hal')}
for wanted in ('android.hardware.graphics.allocator', 'android.hardware.graphics.mapper',
               'android.hardware.health', 'android.hardware.light',
               'android.hardware.sensors', 'android.hardware.keymaster'):
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
assert '/sys/class/backlight/max77696-bl brightness' in ueventd

fstab = (KOA2 / 'rootdir/fstab.koa2').read_text()
assert '/dev/block/' not in fstab
assert 'tmpfs /data' in fstab

se = (KOA2 / 'sepolicy/epdc.te').read_text()
assert 'block_device' not in se

print('KOA2 device tree: koa2.mk copy targets, HIDL manifest, 32-bit binder, 300 PPI, and no-raw-block invariants passed')
