#!/usr/bin/env python3
"""Fix stock printk's missing feature guard in the isolated Binder research tree."""
import argparse
from pathlib import Path
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('source', type=Path)
a = p.parse_args()
if a.source.name != 'linux-binder41' or not (a.source / '.koa2-headless-source-v1').exists():
    raise SystemExit('Only the isolated research source is allowed')
target = a.source / 'kernel/printk/printk.c'
text = target.read_text()
start = 'extern void __dump_lastk_to_mmc(char *buf, unsigned long start, unsigned int size);'
end = 'EXPORT_SYMBOL(dump_lastk_to_mmc);'
guard = '#ifdef CONFIG_LAB126_PRINTK_BUFFER\n' + start
if guard not in text:
    if text.count(start) != 1 or text.count(end) != 1:
        raise SystemExit('Unexpected printk source; refusing patch')
    text = text.replace(start, guard).replace(end, end + '\n#endif /* CONFIG_LAB126_PRINTK_BUFFER */')
    target.write_text(text)
print('Research printk feature guard verified (not hardware validation)')
