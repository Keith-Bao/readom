#!/usr/bin/env python3
"""Compile the pinned stock mass-storage parameter mapping and LUN guard."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile


ROOT = Path(__file__).resolve().parents[2]
STOCK = ROOT / 'ports/koa2/materials/2026-11-21/kernel-source'
GADGET = 'drivers/usb/gadget/function/'


def stock_code(archive):
    lock = json.loads((ROOT / 'ports/koa2/source-lock.json').read_text())
    with archive.open('rb') as stream:
        digest = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    if digest.hexdigest() != lock['kernel']['member_sha256']:
        raise SystemExit('Unexpected stock kernel archive hash')
    with tarfile.open(archive, 'r:gz') as source:
        header = source.extractfile(GADGET + 'f_mass_storage.h').read().decode()
        limits = source.extractfile(GADGET + 'storage_common.h').read().decode()
        code = source.extractfile(GADGET + 'f_mass_storage.c').read().decode()

    parts = [re.search(r'^#define\s+FSG_MAX_LUNS[^\n]+', limits, re.M)[0]]
    for name in ('fsg_module_parameters', 'fsg_lun_config', 'fsg_config'):
        parts.append(re.search(r'^struct ' + name + r' \{.*?^\};', header, re.M | re.S)[0])
    parts.append(re.search(r'^void fsg_config_from_params\(.*?^\}', code, re.M | re.S)[0])
    create_lun = code.split('int fsg_common_create_lun(', 1)[1].split('EXPORT_SYMBOL_GPL', 1)[0]
    guard = re.search(r'^\s*if \(!cfg->filename && !cfg->removable\) \{.*?^\t\}',
                      create_lun, re.M | re.S)[0]
    parts.append(f'''
static int medium_check(struct fsg_lun_config *cfg, int id)
{{
{guard}
    return 0;
}}
''')
    return '\n\n'.join(parts)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, help='verified stock linux-4.1.15.tar.gz')
    args = parser.parse_args()
    compiler = shutil.which(os.environ.get('CC', 'cc'))
    parts = sorted(STOCK.glob('linux-4.1.15.tar.gz.part*'))
    if not compiler or (args.archive is None and not parts):
        print('SKIP needs a host C compiler and the stock source archive')
        return

    with tempfile.TemporaryDirectory() as work:
        work = Path(work)
        archive = args.archive
        if archive is None:
            archive = work / 'linux.tar.gz'
            with archive.open('wb') as output:
                for part in parts:
                    with part.open('rb') as stream:
                        shutil.copyfileobj(stream, output)
        source = work / 'usb-lun.c'
        source.write_text('''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#define CONFIG_LAB126 1
#define min(a,b) ((a) < (b) ? (a) : (b))
#define pr_err(...) fprintf(stderr, __VA_ARGS__)
''' + stock_code(archive) + '''

int main(void)
{
    struct fsg_module_parameters params = {.stall = true};
    struct fsg_config cfg = {0};

    fsg_config_from_params(&cfg, &params, 2);
    assert(cfg.nluns == 1 && cfg.luns[0].filename == NULL);
    assert(medium_check(&cfg.luns[0], 0) == -EINVAL);

    params.removable[0] = true;
    fsg_config_from_params(&cfg, &params, 2);
    assert(cfg.nluns == 1 && cfg.luns[0].filename == NULL);
    assert(cfg.luns[0].removable && cfg.can_stall && cfg.recovery_mode == 0);
    assert(medium_check(&cfg.luns[0], 0) == 0);
    return 0;
}
''')
        probe = work / ('usb-lun.exe' if os.name == 'nt' else 'usb-lun')
        subprocess.run([compiler, '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        str(source), '-o', str(probe)], check=True)
        subprocess.run([str(probe)], check=True)
    print('Stock USB LUN: empty fixed medium refused, empty removable LUN accepted by the guard')


if __name__ == '__main__':
    main()
