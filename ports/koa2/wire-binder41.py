#!/usr/bin/env python3
"""Wire an already reconstructed Binder into an isolated research kernel tree."""
import argparse
import hashlib
import json
import tarfile
import re
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('snapshot', type=Path)
p.add_argument('kernel', type=Path)
a = p.parse_args()
if a.kernel.name != 'linux-binder41' or not (a.kernel / '.koa2-headless-source-v1').is_file():
    raise SystemExit('Only the prepared linux-binder41 research tree is allowed')
receipt = json.loads((a.snapshot / 'receipt.json').read_text())
for path, expected in receipt['files'].items():
    content = (a.snapshot / path).read_bytes()
    if hashlib.sha256(content).hexdigest() != expected:
        raise SystemExit('Snapshot checksum mismatch: ' + path)
names = ('binder.c', 'binder.h', 'binder_trace.h', 'uapi/binder.h')
archive = a.kernel.parent / 'linux-4.1.15.tar.gz'
if hashlib.sha256(archive.read_bytes()).hexdigest() != 'efb1b966fedcba318af381080f6a699642a66a929c42e8578aeb6e3fe240ea93':
    raise SystemExit('Original archive checksum mismatch')
with tarfile.open(archive, 'r:gz') as source:
    native = source.extractfile('drivers/android/binder.c').read().decode()
def adapt(name, data):
    if name != 'binder.c':
        return data
    text = data.decode()
    first = 'static int task_get_unused_fd_flags('
    last = 'static inline void binder_lock('
    text = text[:text.index(first)] + native[native.index(first):native.index(last)] + text[text.index(last):]
    edits = {
        '#include <linux/nsproxy.h>': '#include <linux/nsproxy.h>\n#include <linux/pid_namespace.h>',
        'uid_t binder_context_mgr_uid;': 'kuid_t binder_context_mgr_uid;',
        'uid_t\tsender_euid;': 'kuid_t\tsender_euid;',
        't->sender_euid = proc->tsk->cred->euid;': 't->sender_euid = task_euid(proc->tsk);',
        'tr.sender_euid = t->sender_euid;': 'tr.sender_euid = from_kuid(current_user_ns(), t->sender_euid);',
        'current->nsproxy->pid_ns': 'task_active_pid_ns(current)',
        'context->binder_context_mgr_uid != -1': 'uid_valid(context->binder_context_mgr_uid)',
        'context->binder_context_mgr_uid != current->cred->euid': '!uid_eq(context->binder_context_mgr_uid, current_euid())',
        'context->binder_context_mgr_uid = current->cred->euid;': 'context->binder_context_mgr_uid = current_euid();',
        'binder_device->context.binder_context_mgr_uid = -1;': 'binder_device->context.binder_context_mgr_uid = INVALID_UID;',
        'current->cred->euid,': 'from_kuid(current_user_ns(), current_euid()),',
        'context->binder_context_mgr_uid);': 'from_kuid(current_user_ns(), context->binder_context_mgr_uid));',
    }
    for old, new in edits.items():
        if text.count(old) != 1:
            raise SystemExit('Unexpected Binder adaptation match: ' + old)
        text = text.replace(old, new)
    old_map = '\t\ttmp_area.addr = page_addr;\n\t\ttmp_area.size = PAGE_SIZE + PAGE_SIZE /* guard page? */;\n\t\tpage_array_ptr = page;\n\t\tret = map_vm_area(&tmp_area, PAGE_KERNEL, &page_array_ptr);\n\t\tif (ret) {'
    new_map = '\t\tret = map_kernel_range_noflush((unsigned long)page_addr,\n\t\t\t\tPAGE_SIZE, PAGE_KERNEL, page);\n\t\tflush_cache_vmap((unsigned long)page_addr,\n\t\t\t\t(unsigned long)page_addr + PAGE_SIZE);\n\t\tif (ret != 1) {'
    if text.count(old_map) != 1:
        raise SystemExit('Unexpected legacy Binder page mapping block')
    text = text.replace(old_map, new_map)
    text = text.replace('\tstruct vm_struct tmp_area;\n', '').replace('\t\tstruct page **page_array_ptr;\n', '')
    text = re.sub(r'hlist_for_each_entry\((\w+), pos, ', r'hlist_for_each_entry(\1, ', text)
    text = text.replace('\tstruct hlist_node *pos;\n', '')
    text = text.replace('hlist_for_each_entry_safe(device, node, tmp, ', 'hlist_for_each_entry_safe(device, tmp, ')
    text = text.replace('struct hlist_node *node, *tmp;', 'struct hlist_node *tmp;')
    return text.encode()
with tarfile.open(archive, 'r:gz') as source:
    for name in names:
        target = a.kernel / 'drivers/android' / name
        raw = (a.snapshot / 'drivers/staging/android' / name).read_bytes()
        wanted = adapt(name, raw)
        try:
            member = source.extractfile('drivers/android/' + name)
        except KeyError:
            member = None
        original = member.read() if member else None
        if target.exists() and target.read_bytes() not in (original, raw, wanted):
            previous = {'4e1a11498e86c374ff82d19bdf33d11a9f2dce60530b3fca44e4d6d8a6505204',
                        'b1972f5573750944931bbfced64eb3c47558d5b2e67b064d39dab35509ed62ac'} if name == 'binder.c' else set()
            if hashlib.sha256(target.read_bytes()).hexdigest() not in previous:
                raise SystemExit('Refusing to overwrite local driver changes: ' + str(target))
for name in names:
    target = a.kernel / 'drivers/android' / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(adapt(name, (a.snapshot / 'drivers/staging/android' / name).read_bytes()))
config = a.kernel / 'drivers/android/Kconfig'
text = config.read_text()
if 'config ANDROID_BINDER_DEVICES' not in text:
    config.write_text(text + '\nconfig ANDROID_BINDER_DEVICES\n\tstring "Binder research contexts"\n\tdepends on ANDROID_BINDER_IPC\n\tdefault "binder,hwbinder,vndbinder"\n')
make = a.kernel / 'drivers/android/Makefile'
text = make.read_text()
if 'ccflags-y += -I$(src)' not in text:
    make.write_text(text + '\nccflags-y += -I$(src)\n')
print('Research Binder wired; compile/runtime compatibility still unproven')
