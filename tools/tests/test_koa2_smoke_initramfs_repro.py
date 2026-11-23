#!/usr/bin/env python3
"""The smoke initramfs has to build to the same bytes twice.

The kit script pins what it carries by SHA256 and the device script checks
those sums before loading anything, so an archive whose hash moves on its own
cannot be told apart from one whose contents changed. On 2026-11-23 exactly
that happened: the initramfs in the kit built here and the one in the kit the
device ran on 2026-11-21 had different hashes, and finding out that only seven
bytes differed -- the cpio mtime of /init -- took unpacking both.

The cause is in gen_init_cpio.c as Linux 4.1 ships it: -t sets the mtime it
writes for directories and device nodes (lines 117, 166, 260) but cpio_mkfile
uses the source file's own st_mtime (line 350). So -t 0 is not enough, and the
build script zeroes /init's mtime before packing.

This runs the build script itself, twice, moving the mtime of the compiled init
in between, and requires the two archives to be identical. Running the script
rather than a copy of its packing steps is the point: a test that packs the
archive itself would be checking its own zeroing, pass with the script's
removed, and guard nothing. The first version of this test did exactly that.
"""
import hashlib
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'ports/koa2/build-smoke-initramfs.sh'
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


text = SCRIPT.read_text(encoding='utf-8')
# All three have to stay. Each covers a different timestamp, and the comments in
# the script say which; these are here so that removing one is a failure even on
# a host that cannot run the build.
check('gzip -n' in text, 'the script no longer passes -n to gzip, so the gzip header carries a timestamp')
check('-t 0' in text, 'the script no longer passes -t 0 to gen_init_cpio')
check('touch -d @0' in text, 'the script no longer zeroes the mtime of init before packing')

# Building it needs the cross compiler and the kernel tree the generator comes
# from. Without them, the checks above are all that can be done here.
tree = pathlib.Path(os.environ.get('KOA2_WORK_ROOT', pathlib.Path.home() / 'readom-work'))
if not shutil.which('arm-linux-gnueabi-gcc') or not (tree / 'linux-headless-smoke/usr/gen_init_cpio.c').is_file():
    for failure in failures:
        print('FAIL', failure)
    if failures:
        sys.exit(1)
    print('SKIP needs arm-linux-gnueabi-gcc and the headless-smoke tree; checked the script only')
    sys.exit(0)

# Run the script twice, moving the mtime of the init it compiled in between. The
# script recompiles init, so the mtime it would pack is the build time; moving
# it first is what the old archives differed by.
digests = []
with tempfile.TemporaryDirectory() as home:
    env = dict(os.environ, HOME=home, KOA2_WORK_ROOT=str(tree), LC_ALL='C')
    art = pathlib.Path(home) / 'artifacts/smoke-initramfs'
    for when in (1234567890, 1700000000):
        built = subprocess.run(['bash', str(SCRIPT)], capture_output=True, text=True, env=env)
        check(built.returncode == 0, 'the build script failed: %s' % built.stderr[-300:])
        if built.returncode != 0:
            break
        archive = art / 'initramfs.cpio.gz'
        digests.append(hashlib.sha256(archive.read_bytes()).hexdigest())
        # Move it, so a leak would show in the next build rather than needing
        # two different wall-clock times.
        os.utime(art / 'init', (when, when))
check(len(digests) == 2 and len(set(digests)) == 1,
      'two runs of the build script differ: %s' % ' '.join(d[:16] for d in digests))

# And that -t 0 alone would not do it: with a file entry whose mtime is not
# zeroed, the archive still moves. If this ever stops holding, the kernel tree
# has a gen_init_cpio that covers file entries and the zeroing is redundant.
if digests:
    with tempfile.TemporaryDirectory() as work:
        work = pathlib.Path(work)
        gen = pathlib.Path.home() / 'artifacts/smoke-initramfs/gen_init_cpio'
        if gen.is_file():
            shutil.copy2(gen, work / 'gen_init_cpio')
            (work / 'init').write_bytes(b'x' * 64)
            listing = work / 'list'
            listing.write_text('file /init %s 0755 0 0\n' % (work / 'init'))
            moved = []
            for when in (0, 1234567890):
                os.utime(work / 'init', (when, when))
                cpio = subprocess.run([str(work / 'gen_init_cpio'), '-t', '0', str(listing)],
                                      capture_output=True)
                moved.append(hashlib.sha256(cpio.stdout).hexdigest())
            check(len(set(moved)) == 2,
                  'gen_init_cpio -t 0 now covers file entries, so the zeroing is redundant: %s'
                  % ' '.join(d[:16] for d in moved))

for failure in failures:
    print('FAIL', failure)
if failures:
    sys.exit(1)
print('smoke initramfs: two runs of the build script give the same bytes, and -t 0 alone '
      'still would not cover the /init entry')
