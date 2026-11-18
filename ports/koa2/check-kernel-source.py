#!/usr/bin/env python3
"""Fail before a KOA2 build when required stock display source is absent."""
import argparse
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=Path)
args = parser.parse_args()
required = [
    'Makefile',
    'arch/arm/configs/imx_v7_zelda_defconfig',
    'arch/arm/boot/dts/imx7d-zelda.dts',
    'drivers/video/fbdev/mxc/mxc_epdc_v2_fb.c',
    'drivers/video/fbdev/mxc/mxc_epdc_fb_lab126.c',
    'include/linux/einkwf.h',
    'include/uapi/linux/mxcfb.h',
]
missing = [name for name in required if not (args.source / name).is_file()]
print(json.dumps({'check': 'required-stock-source', 'missing': missing,
                  'passed': not missing,
                  'scope': 'File presence only; not build or hardware compatibility.'}, indent=2))
raise SystemExit(2 if missing else 0)
