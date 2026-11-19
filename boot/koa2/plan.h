/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef KOA2_PLAN_H
#define KOA2_PLAN_H
#include "contract.h"
/* Where a KOA2 handoff puts the new zImage, FDT and initramfs. The handoff
 * stages them in pages it allocates and copies them here last, with the MMU
 * off, after the stock kernel has stopped; so a destination may overwrite the
 * stock kernel, but never anything that has to outlive it (persistent ranges)
 * or anything the new kernel writes while it decompresses and starts. */
enum koa2_segment_kind { KOA2_SEG_ZIMAGE=1, KOA2_SEG_FDT=2, KOA2_SEG_INITRAMFS=3 };
struct koa2_segment { enum koa2_segment_kind kind; uint64_t dest, size; };
/* From ports/koa2/derive-kernel-footprint.py for the exact build being booted. */
struct koa2_footprint {
    uint64_t zreladdr;          /* AUTO_ZRELADDR: (zImage load & 0xf8000000) + TEXT_OFFSET */
    uint64_t kernel_end;        /* physical _end of vmlinux, BSS included */
    uint64_t zimage_size;       /* bytes of the zImage file */
    uint64_t zimage_runtime;    /* bytes past the load address the decompressor uses */
};
#define KOA2_PLAN_MAX_SEGMENTS 3
#define KOA2_PLAN_MAX_RESERVATIONS 16
struct koa2_plan {
    struct koa2_footprint footprint;
    struct koa2_segment segments[KOA2_PLAN_MAX_SEGMENTS];
    size_t segment_count;
    const struct koa2_range *ram; size_t ram_count;
    const struct koa2_range *persistent; size_t persistent_count;
    uint64_t lowmem_end;        /* first physical address the new kernel does not map linearly */
    /* The reserve map of the FDT being passed (koa2_fdt_reservations). */
    struct koa2_range fdt_reservations[KOA2_PLAN_MAX_RESERVATIONS];
    size_t fdt_reservation_count;
};
/* KOA2_OK only when every rule in plan.c holds. Pure: reads the plan, never
 * chooses, copies or jumps. */
int koa2_plan_validate(const struct koa2_plan *plan);
/* The measured exclusions that must survive the jump: printk buffers, ramoops,
 * EPDC reserved-memory and the Falcon BIOS. The running stock kernel, the stock
 * FDT and the stock initrd are dead once the copy starts and are left out. */
#define KOA2_HANDOFF_PERSISTENT_COUNT 4
extern const struct koa2_range koa2_handoff_persistent[KOA2_HANDOFF_PERSISTENT_COUNT];
/* Linux 4.1 VMSPLIT_2G, 240 MiB vmalloc: all 512 MiB is lowmem (boot log
 * "lowmem : 0x80000000 - 0xa0000000"). */
#define KOA2_LOWMEM_END 0xa0000000u
/* Fills plan->segments with zImage, FDT and initramfs (initramfs_size 0: none)
 * packed upward from the first MiB above the kernel footprint, each on its own
 * MiB. Does not validate; the caller must still call koa2_plan_validate. */
int koa2_plan_propose(struct koa2_plan *plan, uint64_t fdt_size, uint64_t initramfs_size);
#endif
