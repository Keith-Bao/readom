/* SPDX-License-Identifier: GPL-2.0-only */
/* Entry for the bare-metal jump test on QEMU -M virt. The MMU is off and the
 * caches are cold at reset, which is the state jump.S requires. */
.syntax unified
.arch armv7-a
.arm
.section .text.start, "ax"
.global _start
_start:
    cpsid if
    ldr sp, =stack_top
    bl baremetal_main
.Lhang:
    b .Lhang

/* The stand-in for the new kernel. It is copied by the list like any payload
 * and entered at its destination, so it must be position independent: no
 * literal pool, no absolute symbol. It checks the register contract, prints,
 * and stops. */
.balign 32
.global fake_kernel_start
fake_kernel_start:
    cmp r0, #0
    bne .Lbad
    ldr r3, .Lmachine_expected - fake_kernel_start + fake_kernel_start
    cmp r1, r3
    bne .Lbad
    /* r2 (the FDT) must be the word pattern the test put there. */
    ldr r3, [r2]
    ldr r4, .Lfdt_expected - fake_kernel_start + fake_kernel_start
    cmp r3, r4
    bne .Lbad
    mov r0, #'K'
    bl .Lputc
    mov r0, #'O'
    bl .Lputc
    mov r0, #'!'
    bl .Lputc
    mov r0, #10
    bl .Lputc
    b .Lstop
.Lbad:
    mov r0, #'X'
    bl .Lputc
    mov r0, #10
    bl .Lputc
.Lstop:
    b .Lstop
/* The PL011 of -M virt. A device address is absolute by nature, so writing it
 * here does not make the code position dependent. */
.Lputc:
    mov r3, #0x09000000
    str r0, [r3]
    bx lr
.balign 4
.Lmachine_expected:
    .word 0xffffffff
.Lfdt_expected:
    .word 0xd00dfeed
.global fake_kernel_end
fake_kernel_end:
.section .bss
.balign 8
.space 4096
stack_top:
.section .note.GNU-stack,"",%progbits
