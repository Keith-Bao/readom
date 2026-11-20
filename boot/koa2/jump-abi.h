/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef KOA2_JUMP_ABI_H
#define KOA2_JUMP_ABI_H
/* Offsets into the parameter block at the end of the trampoline blob. The
 * blob is copied into one page the handoff owns and is entered at its own
 * physical address; it reads its parameters relative to its own PC, so it
 * needs no relocation, no stack and no literal addresses. */
#define KJ_LIST 0           /* physical address of the first list page */
#define KJ_ENTRY 4          /* where the new kernel starts (the zImage destination) */
#define KJ_MACHINE 8        /* r1 for the new kernel */
#define KJ_FDT 12           /* r2 for the new kernel */
#define KJ_RAM_START 16     /* every address in the list must be inside */
#define KJ_RAM_END 20       /* [RAM_START, RAM_END) */
#define KJ_BUDGET 24        /* most list entries to read before giving up. Set it to the
                             * audited entry count: a list the audit accepted then cannot
                             * reach it, and a damaged one stops instead of looping. */
#define KJ_CONTROL 28       /* this page: never a source, destination or list page */
#define KJ_EXPECTED 32      /* pages the list must copy, exactly */
#define KJ_COPIED 36        /* written as it goes */
#define KJ_STATUS 40        /* 0xa1.. progress, 0xe1.. refusal; see jump.S */
#define KJ_BYTES 44

#ifndef __ASSEMBLER__
typedef unsigned int kj_u32;
struct kj_params {
    kj_u32 list, entry, machine, fdt, ram_start, ram_end, budget, control;
    kj_u32 expected, copied, status;
};
typedef char kj_abi_size[(sizeof(struct kj_params) == KJ_BYTES) ? 1 : -1];
/* The blob, as linked here. koa2_jump_start is what gets copied; the caller
 * fills the parameter block in the copy, not in this one. */
extern unsigned char koa2_jump_start[], koa2_jump_end[], koa2_jump_params[];
#endif

/* Status values, in the order they are written. */
#define KJ_STATUS_ENTERED 0xa1
#define KJ_STATUS_MMU_OFF 0xa2
#define KJ_STATUS_COPIED 0xa3
#define KJ_FAIL_SCTLR 0xe1      /* entered with the MMU or caches in a state it will not touch */
#define KJ_FAIL_BUDGET 0xe2     /* more list entries than the caller said */
#define KJ_FAIL_PAGE_END 0xe3   /* ran off the end of a list page with no link */
#define KJ_FAIL_TAG 0xe4        /* an entry with no tag it knows */
#define KJ_FAIL_LINK 0xe5       /* a link outside RAM, unaligned, or the control page */
#define KJ_FAIL_SOURCE 0xe6
#define KJ_FAIL_DEST 0xe7
#define KJ_FAIL_COUNT 0xe9      /* more pages copied than expected */
#define KJ_FAIL_DONE 0xeb       /* the list ended having copied the wrong number */
#endif
