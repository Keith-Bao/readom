#!/usr/bin/env python3
"""The EPDC source gap, held to what ports/koa2/epdc-source-gap.md records.

Three things here are easy to get wrong in a way nothing would notice.

The first is a placeholder. Measuring which symbols mxc_epdc_v2_fb.c expects
from the missing mxc_epdc_fb_lab126.c means putting an empty file there and
reading the compiler's errors, and on 2026-11-21 that is exactly what was done.
An empty file left behind afterwards would make check-kernel-source.py report
the stock profile as complete, and a build would then produce a display driver
with no waveform parsing, no voltage control and no VCOM -- silently, because
every call it needs is satisfied by nothing. So: the checker must still refuse
the stock profile, and must still accept headless-smoke, where the panel is off
and the file is genuinely not needed.

The second is the split between the nine functions. Five sit in the panel's
power and waveform path, where a wrong value has physical consequences; four
only affect logs and debug drawing. Treating those as one list is how "turn it
off and it is fine" gets mixed up with "this has to be right", so the document
names them separately and this keeps both lists present.

The third is what the 2026-11-23 reassessment rests on: lines the missing file
itself printed into the device boot log (the published driver prints none of
them), which give both block copies, the voltage control size and the VCOM
chain. They are recomputed here from the uploaded log rather than read from the
document. Of the five, three reach the hardware when wrong; the other two break
the display. The document also has to keep saying that mxc_set_vcom cannot be
skipped -- its value is the easy one to get, which is exactly why a first
version would drop the call -- and that the night mode waveforms are refused,
because night mode is chosen by whatever waveform userspace asks for.
"""
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
CHECKER = ROOT / 'ports/koa2/check-kernel-source.py'
DOC = ROOT / 'ports/koa2/epdc-source-gap.md'
MISSING = 'drivers/video/fbdev/mxc/mxc_epdc_fb_lab126.c'
# The five whose values reach the panel, and the four that do not.
UNSAFE = ['read_mnt_wfm_v2', 'eink_get_wfm_voltage_control', 'fetch_epdc_vdc_04_data',
          'fetch_epdc_adc_06_data', 'mxc_set_vcom']
SAFE = ['wfm_name_for_mode', 'paint_string', 'mxc_epdc_waveform_proc', 'proc_entry_cleanup']
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


text = DOC.read_text(encoding='utf-8')
for name in UNSAFE + SAFE:
    check('`%s`' % name in text, '%s is not named in the document' % name)
# The document has to keep saying which ones cannot be stubbed, in those words.
check('不能桩掉' in text or 'cannot be stubbed' in text,
      'the document no longer says which functions cannot be stubbed')
# And it has to keep recording the second gap, which is easy to lose.
check('einkwf_panel_' in text, 'the document no longer records the einkwf_panel_* gap')
check('waveform_data_header' in text, 'the document no longer records where the container format is')

# What the 2026-11-23 reassessment rests on, recomputed from the uploaded
# de-identified boot log rather than taken from the document. The missing file
# printed these lines itself: the published driver prints none of them.
LOG = ROOT / 'ports/koa2/materials/2026-11-21/device-evidence/dmesg-before.txt'
log = LOG.read_text(encoding='utf-8', errors='replace') if LOG.is_file() else ''
check(bool(log), 'the uploaded boot log is missing: %s' % LOG)
copies = {}
for m in re.finditer(r'copy \(([0-9a-f]+) - ([0-9a-f]+)\) byte @ offset ([0-9a-f]+) to (acd|vcd) buffer', log):
    end, start, at = (int(m.group(i), 16) for i in (1, 2, 3))
    check(at == start, '%s copy starts at %x but says offset %x' % (m.group(4), start, at))
    copies[m.group(4)] = (start, end)
check(sorted(copies) == ['acd', 'vcd'], 'the log no longer shows both block copies: %s' % sorted(copies))
size = re.search(r'Retrieved voltage control information \(0x([0-9a-f]+)\)', log)
check(size is not None, 'the log no longer shows the voltage control size')
if size and 'vcd' in copies:
    start, end = copies['vcd']
    check(end - start == int(size.group(1), 16),
          'the vcd block is %d bytes but the voltage control size is 0x%s' % (end - start, size.group(1)))
if sorted(copies) == ['acd', 'vcd']:
    check(copies['vcd'][1] == copies['acd'][0], 'the vcd block no longer ends where the acd block starts')
idme = re.search(r'idme_init: vcom = (-?[0-9]+)', log)
setv = re.search(r'mxc_set_vcom: vcom_uV=(-?[0-9]+)', log)
check(bool(idme and setv) and idme.group(1) == setv.group(1),
      'the log no longer shows the IDME VCOM reaching mxc_set_vcom unchanged')
check('read_mnt_wfm_v2: waveform at /mnt/wfm/' in log,
      'the log no longer shows the waveform read from a file')

# The three whose wrong values reach the hardware. mxc_set_vcom is the easy one
# to get right, which is why it is the one a first version would leave out.
POWER = ['eink_get_wfm_voltage_control', 'fetch_epdc_vdc_04_data', 'mxc_set_vcom']
HEAD, TAIL = '## 风险重新评估', '## 现在的判断'
section = text[text.index(HEAD):text.index(TAIL)] if HEAD in text and TAIL in text else ''
check(bool(section), 'the reassessment section is gone')
for name in POWER:
    check('`%s`' % name in section, '%s is not named in the reassessment' % name)
check('`mxc_set_vcom` 仍然不能桩' in section,
      'the document no longer says mxc_set_vcom cannot be skipped')
for mode in ('GCK16', 'GLKW16'):
    check(mode in section, 'the reassessment no longer names the night mode waveform %s' % mode)

# The gate: refuse stock, accept headless-smoke. Run against whatever kernel
# tree this host has; without one, say so rather than pass.
tree = pathlib.Path.home() / 'readom-work/linux-headless-smoke'
if not (tree / 'drivers/video/fbdev/mxc/mxc_epdc_v2_fb.c').is_file():
    for failure in failures:
        print('FAIL', failure)
    if failures:
        sys.exit(1)
    print('SKIP the kernel tree is not on this host; checked the document and the device log only')
    sys.exit(0)

stub = tree / MISSING
check(not stub.exists(),
      'a placeholder for the missing file is in the tree: the stock profile would '
      'wrongly pass and a build would make a driver with no waveform parsing')

for args, want_pass, label in (([], False, 'stock'), (['--headless-smoke'], True, 'headless-smoke')):
    p = subprocess.run([sys.executable, '-I', str(CHECKER), str(tree), *args],
                       capture_output=True, text=True)
    report = json.loads(p.stdout) if p.stdout.strip().startswith('{') else None
    check(report is not None, '%s: no report (%s)' % (label, p.stderr[-200:]))
    if report is None:
        continue
    check(report['passed'] == want_pass,
          '%s: passed=%s, expected %s' % (label, report['passed'], want_pass))
    check((p.returncode == 0) == want_pass, '%s: exit %d' % (label, p.returncode))
    if not want_pass:
        check(MISSING in report['missing'],
              'the stock profile should name the missing file, got %s' % report['missing'])

for failure in failures:
    print('FAIL', failure)
if failures:
    sys.exit(1)
print('EPDC gap: no placeholder in the tree, the stock profile is still refused and '
      'headless-smoke still accepted, and the document still separates the five '
      'functions that reach the panel from the four that do not; the block layout '
      'and the VCOM chain recompute from the device log, and mxc_set_vcom and the '
      'night mode refusal are still named')
