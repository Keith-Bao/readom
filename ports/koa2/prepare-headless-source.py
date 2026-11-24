#!/usr/bin/env python3
"""Prepare a separate, verified source tree for the experimental kernel."""
import argparse
import hashlib
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('archive', type=Path)
p.add_argument('destination', type=Path)
a = p.parse_args()
expected = 'efb1b966fedcba318af381080f6a699642a66a929c42e8578aeb6e3fe240ea93'
if hashlib.sha256(a.archive.read_bytes()).hexdigest() != expected:
    raise SystemExit('Stock kernel archive checksum mismatch')
marker = a.destination / '.koa2-headless-source-v1'
# (file, stock text, experimental text). The stock text must occur exactly once;
# an edit whose experimental text is already present is not applied again.
EDITS = [
    # Only the experimental suffix is appended; the stock release string is untouched.
    ('scripts/setlocalversion',
     'res="-lab126"\necho "$res"',
     'res="-lab126"\nif test "$CONFIG_LOCALVERSION" = "-koa2-headless-smoke"; then\n'
     '\tres="${res}${CONFIG_LOCALVERSION}"\nfi\necho "$res"'),
    # Without FALCON_BLK there is no __dump_lastk_to_mmc and no raw eMMC write on
    # restart or on the power-key battery-cut warning (ports/koa2/handoff-quiesce.md).
    ('kernel/printk/printk.c',
     'void dump_lastk_to_mmc(void)\n{\n',
     'void dump_lastk_to_mmc(void)\n{\n#ifdef CONFIG_FALCON_BLK\n'),
    ('kernel/printk/printk.c',
     '\t}\n}\nEXPORT_SYMBOL(dump_lastk_to_mmc);',
     '\t}\n#endif\n}\nEXPORT_SYMBOL(dump_lastk_to_mmc);'),
    # Clean the Lab126 printk buffer (0x9be00000) out of inner and outer D-caches
    # on boot-time copy/clear and on every log_store(), because a hardware WDOG_B
    # reset does not flush caches (ports/koa2/recovery.md; cf. PW3 patch 1043).
    ('kernel/printk/printk.c',
     '#include <asm/uaccess.h>\n',
     '#include <asm/uaccess.h>\n'
     '#if defined(CONFIG_LAB126_PRINTK_BUFFER) && defined(CONFIG_ARM)\n'
     '#include <asm/cacheflush.h>\n'
     '#include <asm/outercache.h>\n'
     '#endif\n'),
    ('kernel/printk/printk.c',
     'static char *last_log_buffer = NULL;\n#endif',
     'static char *last_log_buffer = NULL;\n'
     'static phys_addr_t log_goal_phys;\n'
     'static inline void log_goal_write_back(char *va, size_t len)\n'
     '{\n'
     '#ifdef CONFIG_ARM\n'
     '\tif (log_goal_phys && len) {\n'
     '\t\tphys_addr_t pa = log_goal_phys + (va - log_goal_first);\n'
     '\t\t__cpuc_flush_dcache_area(va, len);\n'
     '\t\touter_clean_range(pa, pa + len);\n'
     '\t}\n'
     '#endif\n'
     '}\n'
     '#endif'),
    ('kernel/printk/printk.c',
     '\t\t/* no else here */ if (msg->text_len < (log_goal_last - log_goal_idx)) {\n'
     '\t\t\tmemcpy(log_goal_idx, log_text(msg), msg->text_len);\n'
     '\t\t\tlog_goal_idx += msg->text_len;\n'
     '\t\t\t*log_goal_idx++ = \'\\n\';\n'
     '\t\t}',
     '\t\t/* no else here */ if (msg->text_len < (log_goal_last - log_goal_idx)) {\n'
     '\t\t\tchar *start = log_goal_idx;\n'
     '\t\t\tmemcpy(log_goal_idx, log_text(msg), msg->text_len);\n'
     '\t\t\tlog_goal_idx += msg->text_len;\n'
     '\t\t\t*log_goal_idx++ = \'\\n\';\n'
     '\t\t\tlog_goal_write_back(start, msg->text_len + 1);\n'
     '\t\t}'),
    ('kernel/printk/printk.c',
     '\t\t\tlast_log_buffer = log_goal_first + LOG_GOAL_SIZE;\n\n'
     '\t\t\tmemcpy(last_log_buffer, log_goal_first, LOG_GOAL_SIZE);\n\n'
     '\t\t\tprintk("Prink buffer had been relocated to physical address: 0x%08lx\\n",\n'
     '\t\t\t\tlog_goal_location);\n\n'
     '\t\t\t//Now that it is saved, clear out last time\'s prink to prevent log framents from\n'
     '\t\t\t//recusrively showing up in printk logs.\n'
     '\t\t\tmemset(log_goal_first, \'\\0\', LOG_GOAL_SIZE);',
     '\t\t\tlast_log_buffer = log_goal_first + LOG_GOAL_SIZE;\n'
     '\t\t\tlog_goal_phys = log_goal_location;\n\n'
     '\t\t\tmemcpy(last_log_buffer, log_goal_first, LOG_GOAL_SIZE);\n'
     '\t\t\tlog_goal_write_back(last_log_buffer, LOG_GOAL_SIZE);\n\n'
     '\t\t\tprintk("Prink buffer had been relocated to physical address: 0x%08lx\\n",\n'
     '\t\t\t\tlog_goal_location);\n\n'
     '\t\t\t//Now that it is saved, clear out last time\'s prink to prevent log framents from\n'
     '\t\t\t//recusrively showing up in printk logs.\n'
     '\t\t\tmemset(log_goal_first, \'\\0\', LOG_GOAL_SIZE);\n'
     '\t\t\tlog_goal_write_back(log_goal_first, LOG_GOAL_SIZE);'),
    # usdhc3 keeps the chip dtsi compatible, so sdhci-esdhc-imx owns the eMMC.
    ('arch/arm/boot/dts/imx7d-zelda.dts',
     '&usdhc3 {\n\tcompatible = "falcon_blk";\n',
     '&usdhc3 {\n\t/* koa2-headless-smoke: standard sdhci-esdhc-imx, not the Falcon BIOS */\n'),
]
if not marker.exists():
    if a.destination.exists() and any(a.destination.iterdir()):
        raise SystemExit('Refusing to modify an existing unverified source tree')
    a.destination.mkdir(parents=True, exist_ok=True)
    subprocess.run(['tar', '--no-same-owner', '-xzf', str(a.archive), '-C', str(a.destination)], check=True)
    marker.write_text(expected + '\n')
if marker.read_text().strip() != expected:
    raise SystemExit('Prepared source marker does not match the stock archive')
for name, old, new in EDITS:
    target = a.destination / name
    text = target.read_text()
    if new in text:
        continue
    if text.count(old) != 1:
        raise SystemExit(f'Unexpected stock {name}; no edit applied')
    target.write_text(text.replace(old, new))
for name, old, new in EDITS:
    if new not in (a.destination / name).read_text():
        raise SystemExit(f'Experimental edit verification failed: {name}')
print('Verified experimental source:', a.destination)
