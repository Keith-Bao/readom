#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Static contract checks for the clean-room v1 KOA2 EPDC Lab126 helpers."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'ports/koa2/epdc-v1/mxc_epdc_fb_lab126.c'
BUILD = ROOT / 'ports/koa2/epdc-v1/build-epdc-v1-check.sh'


class EpdcV1ContractTests(unittest.TestCase):
    def test_defines_all_nine_helpers(self):
        text = SRC.read_text(encoding='utf-8')
        for fn in (
            'read_mnt_wfm_v2(',
            'mxc_set_vcom(',
            'eink_get_wfm_voltage_control(',
            'fetch_epdc_vdc_04_data(',
            'fetch_epdc_adc_06_data(',
            'wfm_name_for_mode(',
            'paint_string(',
            'mxc_epdc_waveform_proc(',
            'proc_entry_cleanup(',
        ):
            self.assertIn(fn, text, fn)

    def test_vcom_uses_max77696_vid_and_bounds(self):
        text = SRC.read_text(encoding='utf-8')
        self.assertIn('MAX77696_EPD_VID(vcom_uV)', text)
        self.assertIn('KOA2_EPDC_VCOM_MIN_UV', text)
        self.assertIn('KOA2_EPDC_VCOM_MAX_UV', text)
        self.assertIn('regulator_set_voltage', text)

    def test_v1_does_not_touch_nightmode_rails_and_rejects_nightmode(self):
        src_text = re.sub(r'/\*.*?\*/', '', SRC.read_text(encoding='utf-8'), flags=re.S)
        self.assertNotIn('v3p3_regulator', src_text)
        self.assertNotIn('xon_len', src_text)
        self.assertNotIn('vpdd_len', src_text)
        build_text = BUILD.read_text(encoding='utf-8')
        self.assertIn('WAVEFORM_MODE_GCK16', build_text)
        self.assertIn('WAVEFORM_MODE_GLKW16', build_text)
        self.assertIn('GRAYSCALE_8BIT_INVERTED', build_text)
        self.assertIn('return -EOPNOTSUPP;', build_text)


if __name__ == '__main__':
    res = unittest.main(exit=False, verbosity=0).result
    if not res.wasSuccessful():
        raise SystemExit(1)
    print('KOA2 EPDC v1 contract: 9 helpers, VCOM VID/bounds, night-mode hard reject')
