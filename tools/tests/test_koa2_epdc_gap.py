#!/usr/bin/env python3
"""The EPDC source gap, held to what ports/koa2/epdc-source-gap.md records.

Two things here are easy to get wrong in a way nothing would notice.

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
"""
import json
import pathlib
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

# The gate: refuse stock, accept headless-smoke. Run against whatever kernel
# tree this host has; without one, say so rather than pass.
tree = pathlib.Path.home() / 'readom-work/linux-headless-smoke'
if not (tree / 'drivers/video/fbdev/mxc/mxc_epdc_v2_fb.c').is_file():
    for failure in failures:
        print('FAIL', failure)
    if failures:
        sys.exit(1)
    print('SKIP the kernel tree is not on this host; checked the document only')
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
      'functions that reach the panel from the four that do not')
