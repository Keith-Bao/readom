#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify the KOA2 headless kernel printk buffer cache write-back edits."""
import ast
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
PREPARE = ROOT / 'ports/koa2/prepare-headless-source.py'


def load_edits():
    tree = ast.parse(PREPARE.read_text(encoding='utf-8'))
    for node in tree.body:
        if isinstance(node, ast.Assign):
            for target in node.targets:
                if isinstance(target, ast.Name) and target.id == 'EDITS':
                    return ast.literal_eval(node.value)
    raise AssertionError('EDITS not found in prepare-headless-source.py')


class PrintkWriteBackTests(unittest.TestCase):
    def test_edits_clean_inner_and_outer_caches(self):
        edits = [e for e in load_edits() if e[0] == 'kernel/printk/printk.c']
        combined = '\n'.join(new for _, _, new in edits)
        self.assertIn('#include <asm/cacheflush.h>', combined)
        self.assertIn('#include <asm/outercache.h>', combined)
        self.assertIn('__cpuc_flush_dcache_area(va, len);', combined)
        self.assertIn('outer_clean_range(pa, pa + len);', combined)
        self.assertIn('log_goal_write_back(start, msg->text_len + 1);', combined)
        self.assertIn('log_goal_write_back(last_log_buffer, LOG_GOAL_SIZE);', combined)
        self.assertIn('log_goal_write_back(log_goal_first, LOG_GOAL_SIZE);', combined)
        self.assertIn('#ifdef CONFIG_FALCON_BLK', combined)

    def test_edits_apply_once_and_are_idempotent_on_fixture(self):
        fixture = (
            '#include <asm/uaccess.h>\n'
            'static char *last_log_buffer = NULL;\n#endif\n'
            'void dump_lastk_to_mmc(void)\n{\n\t}\n}\nEXPORT_SYMBOL(dump_lastk_to_mmc);\n'
            '\t\t/* no else here */ if (msg->text_len < (log_goal_last - log_goal_idx)) {\n'
            '\t\t\tmemcpy(log_goal_idx, log_text(msg), msg->text_len);\n'
            '\t\t\tlog_goal_idx += msg->text_len;\n'
            '\t\t\t*log_goal_idx++ = \'\\n\';\n'
            '\t\t}\n'
            '\t\t\tlast_log_buffer = log_goal_first + LOG_GOAL_SIZE;\n\n'
            '\t\t\tmemcpy(last_log_buffer, log_goal_first, LOG_GOAL_SIZE);\n\n'
            '\t\t\tprintk("Prink buffer had been relocated to physical address: 0x%08lx\\n",\n'
            '\t\t\t\tlog_goal_location);\n\n'
            '\t\t\t//Now that it is saved, clear out last time\'s prink to prevent log framents from\n'
            '\t\t\t//recusrively showing up in printk logs.\n'
            '\t\t\tmemset(log_goal_first, \'\\0\', LOG_GOAL_SIZE);\n'
        )
        edits = [e for e in load_edits() if e[0] == 'kernel/printk/printk.c']
        text = fixture
        for _, old, new in edits:
            self.assertEqual(text.count(old), 1)
            text = text.replace(old, new)
        once = text
        for _, old, new in edits:
            if new in text:
                continue
            self.fail(f'edit not idempotent: {old!r}')
        self.assertEqual(text, once)


if __name__ == '__main__':
    res = unittest.main(exit=False, verbosity=0).result
    if not res.wasSuccessful():
        raise SystemExit(1)
    print('KOA2 printk write-back: inner/outer cache clean on copy, clear and store; idempotent')
