/* SPDX-License-Identifier: GPL-2.0-only */
/* KOA2 handoff trampoline: turns off the MMU, walks the copy list, enters the
 * new kernel. Reviewed and tested on the host and bare-metal in QEMU; it is
 * NOT built into the staging module, which has no way to reach it.
 *
 * What the caller must have done, none of which this checks for it:
 *   - one core online (the second taken down with the stock cpu_down)
 *   - SVC mode, ARM state, IRQ and FIQ masked
 *   - every DMA master quiesced (ports/koa2/handoff-quiesce.md)
 *   - caches cleaned to RAM and disabled, TTBCR 0
 *   - this blob copied into one page the handoff owns, entered at that page's
 *     physical address, which on KOA2 is also its virtual address because
 *     PAGE_OFFSET == PHYS_OFFSET and lowmem is identity mapped. That is why
 *     there is no private page table here: clearing the M bit does not move
 *     the instruction stream.
 *   - a copy list koa2_copylist_audit accepted
 *
 * It has no stack, no external call, no literal pool and no writable data
 * besides its own parameter block. A refusal records a status and stops: there
 * is no going back to a kernel whose caches are already off.
 */
#ifndef KOA2_JUMP_BUILD
#error "jump.S is review-only unless KOA2_JUMP_BUILD is defined"
#endif
#include "jump-abi.h"
.syntax unified
.arch armv7-a
.arm
.text
.balign 32
.global koa2_jump_start
koa2_jump_start:
    /* r12 = the parameter block in this copy of the blob, found by PC. */
    adr r12, koa2_jump_start
    add r12, r12, #(koa2_jump_params - koa2_jump_start)
    mov r1, #KJ_STATUS_ENTERED
    str r1, [r12, #KJ_STATUS]
    mov r1, #0
    str r1, [r12, #KJ_COPIED]
    /* The caller said it had disabled the caches; if not, stop rather than
     * copy through stale lines. Bit 2 is C, bit 12 is I. */
    mrc p15, 0, r1, c1, c0, 0
    tst r1, #(1 << 2)
    bne .Lsctlr_bad
    tst r1, #(1 << 12)
    bne .Lsctlr_bad
    dsb
    isb
    /* Clear the M bit. The next instruction is fetched from the same physical
     * address as this one. */
    bic r1, r1, #1
    mcr p15, 0, r1, c1, c0, 0
    isb
    mov r1, #0
    mcr p15, 0, r1, c8, c7, 0       @ invalidate the whole TLB
    dsb
    isb
    mov r1, #KJ_STATUS_MMU_OFF
    str r1, [r12, #KJ_STATUS]
    /* r7 ram_start, r8 ram_end, r9 budget, r11 control page, r4 destination,
     * r0 the list cursor, r10 the end of the current list page. */
    ldr r7, [r12, #KJ_RAM_START]
    ldr r8, [r12, #KJ_RAM_END]
    ldr r9, [r12, #KJ_BUDGET]
    ldr r11, [r12, #KJ_CONTROL]
    ldr r3, [r12, #KJ_LIST]
    mov r4, #0
    b .Llink                        @ the head is a list page like any other
.Lnext:
    subs r9, r9, #1
    bcc .Lbudget_bad
    cmp r0, r10
    bhs .Lpage_end_bad
    ldr r3, [r0], #4
    and r2, r3, #0xf
    bic r3, r3, #0xf
    cmp r2, #4                      @ KOA2_COPY_DONE
    beq .Ldone
    cmp r2, #1                      @ KOA2_COPY_DEST
    beq .Ldestination
    cmp r2, #2                      @ KOA2_COPY_LINK
    beq .Llink
    cmp r2, #8                      @ KOA2_COPY_SOURCE
    beq .Lsource
    mov r1, #KJ_FAIL_TAG
    b .Lfail
/* A page address this code will read from or write to: page-aligned, a whole
 * page inside RAM, and not this page. Called with the candidate in r3 and the
 * status to record on refusal in r1; falls through when it is good. */
.macro check_page fail
    /* Page-aligned. 0xfff is not an encodable immediate, so shift the low
     * bits out of the word and test what is left. */
    movs r2, r3, lsl #20
    bne \fail
    cmp r3, r7
    blo \fail
    sub r2, r8, #4096
    cmp r3, r2
    bhi \fail
    cmp r3, r11
    beq \fail
.endm
.Llink:
    check_page .Llink_bad
    mov r0, r3
    add r10, r3, #4096
    sub r10, r10, #4                @ the last word of a page may hold a link
    b .Lnext
.Ldestination:
    check_page .Ldest_bad
    mov r4, r3
    b .Lnext
.Lsource:
    check_page .Lsource_bad
    /* The destination has to have been set and still be a page in RAM: the
     * list advances it by a page per source. */
    mov r5, r3
    mov r3, r4
    check_page .Ldest_bad
    mov r3, r5
    ldr r1, [r12, #KJ_COPIED]
    ldr r2, [r12, #KJ_EXPECTED]
    cmp r1, r2
    bhs .Lcount_bad
    add r1, r1, #1
    str r1, [r12, #KJ_COPIED]
    mov r6, #(4096 / 32)
.Lcopy:
    ldmia r3!, {r1, r2, r5}
    stmia r4!, {r1, r2, r5}
    ldmia r3!, {r1, r2, r5}
    stmia r4!, {r1, r2, r5}
    ldmia r3!, {r1, r2}
    stmia r4!, {r1, r2}
    subs r6, r6, #1
    bne .Lcopy
    b .Lnext
.Ldone:
    ldr r1, [r12, #KJ_COPIED]
    ldr r2, [r12, #KJ_EXPECTED]
    cmp r1, r2
    bne .Ldone_bad
    mov r1, #KJ_STATUS_COPIED
    str r1, [r12, #KJ_STATUS]
    dsb
    isb
    /* The new kernel's entry contract: r0 zero, r1 the machine number, r2 the
     * FDT. head.S reads nothing else. */
    ldr lr, [r12, #KJ_ENTRY]
    ldr r1, [r12, #KJ_MACHINE]
    ldr r2, [r12, #KJ_FDT]
    mov r0, #0
    bx lr
.Lsctlr_bad:
    mov r1, #KJ_FAIL_SCTLR
    b .Lfail
.Lbudget_bad:
    mov r1, #KJ_FAIL_BUDGET
    b .Lfail
.Lpage_end_bad:
    mov r1, #KJ_FAIL_PAGE_END
    b .Lfail
.Llink_bad:
    mov r1, #KJ_FAIL_LINK
    b .Lfail
.Lsource_bad:
    mov r1, #KJ_FAIL_SOURCE
    b .Lfail
.Ldest_bad:
    mov r1, #KJ_FAIL_DEST
    b .Lfail
.Lcount_bad:
    mov r1, #KJ_FAIL_COUNT
    b .Lfail
.Ldone_bad:
    mov r1, #KJ_FAIL_DONE
.Lfail:
    str r1, [r12, #KJ_STATUS]
    dsb
.Lhalt:
    b .Lhalt
.balign 4
.global koa2_jump_params
koa2_jump_params:
    .space KJ_BYTES
.global koa2_jump_end
koa2_jump_end:
.section .note.GNU-stack,"",%progbits
