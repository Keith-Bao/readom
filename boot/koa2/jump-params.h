/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef KOA2_JUMP_PARAMS_H
#define KOA2_JUMP_PARAMS_H
#include "koa2-types.h"
#include "jump-abi.h"
/* Fills the trampoline parameter block (jump-abi.h) and checks the addresses
 * it is given are the kind jump.S assumes: page-aligned, whole pages inside the
 * one RAM window, and a control page distinct from the list head and the
 * entry. It is pure -- it reads and writes the block and nothing else, touches
 * no hardware and does not choose addresses -- so the module and the bare-metal
 * test fill the block through the same code. It is NOT the copy-list audit:
 * koa2_copylist_audit is what rules a source or list page off a destination.
 *
 * What it cannot see (the caller must): that list/entry/fdt/control actually
 * point at the staged list, the zImage destination, the FDT destination and a
 * page the handoff owns. It only checks their shape. */
struct koa2_jump_inputs {
    uint32_t list;      /* physical address of the first copy-list page */
    uint32_t entry;     /* zImage destination, where the new kernel starts */
    uint32_t machine;   /* r1 for the new kernel */
    uint32_t fdt;       /* r2 for the new kernel */
    uint32_t ram_start; /* every address must be inside [ram_start, ram_end) */
    uint32_t ram_end;
    uint32_t budget;    /* list entries to read before giving up; the audited count */
    uint32_t control;   /* the page the blob runs from; never a source/dest/list page */
    uint32_t expected;  /* pages the list must copy, exactly */
};
int koa2_jump_fill(struct kj_params *out, const struct koa2_jump_inputs *in);
#endif
