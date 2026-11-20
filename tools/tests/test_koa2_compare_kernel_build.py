#!/usr/bin/env python3
"""ports/koa2/compare-kernel-build.py, on build outputs made here.

Two builds of the same KOA2 source and config do not produce the same bytes,
for two reasons that have nothing to do with the kernel:

  * arch/arm/boot/Makefile pipes Image into `lzop -9`, and lzop reading a pipe
    stamps the current time into its header, so piggy.lzo and the zImage differ
    on every build;
  * the linker writes a GNU build-id into .notes derived from the object
    files, whose paths differ between output directories.

For twelve days STATUS.md recorded the first of these as an unexplained
difference between two build hosts and said their output could not be claimed
reproducible. It was never a difference in the kernel. This holds the tool to
saying so: identical but for those two things is "same kernel", and a real
change in the kernel is not.
"""
import json
import pathlib
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOL = ROOT / 'ports/koa2/compare-kernel-build.py'
LZOP_MAGIC = bytes.fromhex('894c5a4f000d0a1a0a')
TEXT = 0x80008000
failures = []


def check(ok, message):
    if not ok:
        failures.append(message)


def build(out, body, build_id, mtime, notes_at=0x1000):
    """A stand-in build output: Image with a build-id note, a zImage with an
    lzop header around a payload, System.map, and piggy.lzo."""
    (out / 'arch/arm/boot/compressed').mkdir(parents=True, exist_ok=True)
    image = bytearray(body)
    note = bytearray(struct.pack('<III', 4, 20, 3) + b'GNU\0' + build_id)
    image[notes_at:notes_at + len(note)] = note
    (out / 'arch/arm/boot/Image').write_bytes(bytes(image))
    (out / 'System.map').write_text(
        '%08x T _text\n%08x T __start_notes\n%08x T __stop_notes\n'
        % (TEXT, TEXT + notes_at, TEXT + notes_at + len(note)))
    # lzop checksums its own header, so the mtime has to come from lzop: it
    # takes it from the input file, which is also what the kernel's pipe makes
    # it invent from the clock.
    source = out / 'image-for-lzop'
    source.write_bytes(bytes(image))
    import os
    os.utime(source, (mtime, mtime))
    payload = bytearray(subprocess.run(['lzop', '-9', '-c', str(source)],
                                       capture_output=True, check=True).stdout)
    source.unlink()
    trailer = len(image).to_bytes(4, 'little')
    (out / 'arch/arm/boot/compressed/piggy.lzo').write_bytes(bytes(payload) + trailer)
    (out / 'arch/arm/boot/zImage').write_bytes(b'HEAD' * 8 + bytes(payload) + trailer)
    return out


def run(*outs):
    p = subprocess.run([sys.executable, '-I', str(TOOL), *map(str, outs)], capture_output=True, text=True)
    return p.returncode, (json.loads(p.stdout) if p.stdout.strip().startswith('{') else None), p.stderr


with tempfile.TemporaryDirectory() as tmp:
    t = pathlib.Path(tmp)
    body = bytes(range(256)) * 200
    a = build(t / 'a', body, bytes(range(20)), 1795157195)
    b = build(t / 'b', body, bytes(range(20, 40)), 1795157370)
    code, r, err = run(a, b)
    check(code == 0 and r, f'two builds of one kernel: {code} {err}')
    if r:
        check(r['verdict'] == 'same kernel', f'verdict {r["verdict"]!r}')
        check(not r['image_identical'] and r['image_identical_but_for_the_build_id'],
              'the build-id alone should differ')
        check(not r['zimage_identical'], 'the zImage should differ (lzop mtime)')
        check(len(r['build_ids']) == 2, f'build ids {r["build_ids"]}')
        check(r['builds'][0]['payload_matches_image'], 'payload should match Image')
        check(r['builds'][0]['lzop_mtime'] == 1795157195, 'mtime should be read out')
    # A kernel that really differs is not the same kernel.
    c = build(t / 'c', body[:-1] + bytes([0x7f]), bytes(range(20)), 1795157195)   # body ends in 0xff
    code, r, _ = run(a, c)
    check(code == 1 and r and r['verdict'] == 'different kernels', f'changed kernel: {code} {r and r["verdict"]}')
    # The same output twice is identical in every field.
    code, r, _ = run(a, a)
    check(code == 0 and r['image_identical'] and r['zimage_identical'], 'one build against itself')
    # Refusals: a missing Image, and a payload that is not the Image beside it.
    (t / 'd/arch/arm/boot').mkdir(parents=True)
    code, r, err = run(t / 'd')
    check(code != 0 and 'missing' in err, f'missing Image: {code} {err}')
    e = build(t / 'e', body, bytes(range(20)), 1795157195)
    (e / 'arch/arm/boot/Image').write_bytes(body[:-1] + b'\x00')
    code, r, err = run(e)
    check(code != 0 and 'not Image' in err or 'does not decompress' in err, f'payload mismatch: {code} {err}')

for failure in failures:
    print('FAIL', failure)
if failures:
    sys.exit(1)
print('compare-kernel-build: the lzop timestamp and the build-id are not kernel differences, and a real one is')
