/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef KOA2_FDT_REWRITE_H
#define KOA2_FDT_REWRITE_H
#include "contract.h"
/* What the handoff changes in the stock runtime FDT before passing it on.
 * Everything else (board nodes, reserved-memory, /memory) is copied as is. */
struct koa2_fdt_edit {
    const char *bootargs;            /* replaces /chosen/bootargs; required */
    uint64_t initrd_start, initrd_end; /* both 0: no initramfs, the old ones are dropped */
    const struct koa2_range *reserve; size_t reserve_count; /* the new header reserve map */
    unsigned rewrite_usdhc3_standard; /* 1: switch usdhc@30b60000 from falcon_blk to fsl,imx7d-usdhc */
};
/* What the rewrite saw on its way through, for the caller to check. */
struct koa2_fdt_seen {
    unsigned memory_regs;            /* /memory reg properties found */
    uint64_t memory_start, memory_size; /* the last one, if it is one cell each */
    unsigned chosen_nodes;           /* 1 if /chosen existed, 0 if one was added */
    unsigned usdhc3_rewritten;       /* count of usdhc@30b60000 compatible props rewritten */
};
#define KOA2_BOOTARGS_MAX 1023       /* arch/arm COMMAND_LINE_SIZE 1024 with its NUL */
/* Writes a new FDT into out: header reserve map replaced, /chosen bootargs and
 * linux,initrd-start/end replaced (new properties first, before any subnode),
 * every other token copied. Fails without a partial result (*bytes 0) if the
 * input does not pass koa2_fdt_validate, the edit is out of range, out is too
 * small, or the result does not validate. Pure: no allocation. */
int koa2_fdt_rewrite(const void *in, size_t in_bytes, const struct koa2_fdt_edit *edit,
                     void *out, size_t out_max, size_t *bytes, struct koa2_fdt_seen *seen);
#endif
