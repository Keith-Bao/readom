/* SPDX-License-Identifier: GPL-2.0-only */
/* QEMU -M virt puts RAM at 0x40000000. The test keeps its own code and data in
 * the first MiB and uses whole pages above it for the control page, the list,
 * the sources and the destinations, so no copy can land on the test itself. */
ENTRY(_start)
SECTIONS
{
    . = 0x40010000;
    .text : { *(.text.start) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
    . = ALIGN(4096);
    _end = .;
}
