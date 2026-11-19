/* SPDX-License-Identifier: GPL-2.0-only */
#include "plan.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MIB 0x100000u
/* ports/koa2/derive-kernel-footprint.py on the headless-smoke build (zImage
 * sha256 4261b0e2...). A different build needs its own numbers. */
static const struct koa2_footprint smoke={0x80008000u,0x80c55a0cu,6652856u,6722520u};
static struct koa2_plan base(void) {
    struct koa2_plan p;
    memset(&p,0,sizeof(p));
    p.footprint=smoke;
    p.ram=koa2_measured_ram; p.ram_count=KOA2_MEASURED_RAM_COUNT;
    p.persistent=koa2_handoff_persistent; p.persistent_count=KOA2_HANDOFF_PERSISTENT_COUNT;
    p.lowmem_end=KOA2_LOWMEM_END;
    /* smoke FDT 51008 bytes, smoke initramfs 271689 bytes */
    assert(koa2_plan_propose(&p,51008,271689)==0);
    return p;
}
static void persistent_is_measured_subset(void) {
    size_t i,j;
    for(i=0;i<KOA2_HANDOFF_PERSISTENT_COUNT;i++) {
        int found=0;
        for(j=0;j<KOA2_MEASURED_EXCLUDED_COUNT;j++)
            if(!memcmp(&koa2_handoff_persistent[i],&koa2_measured_excluded[j],sizeof(struct koa2_range))) found=1;
        assert(found);
    }
}
int main(void) {
    struct koa2_plan p=base(),q;
    persistent_is_measured_subset();
    /* The proposed layout, written out so a change to propose() shows here. */
    assert(p.segment_count==3);
    assert(p.segments[0].kind==KOA2_SEG_ZIMAGE && p.segments[0].dest==0x80d00000u);
    assert(p.segments[1].kind==KOA2_SEG_FDT && p.segments[1].dest==0x81400000u);
    assert(p.segments[2].kind==KOA2_SEG_INITRAMFS && p.segments[2].dest==0x81500000u);
    assert(koa2_plan_validate(&p)==0);
    /* The FDT may reserve itself, the initramfs and persistent ranges ... */
    p.fdt_reservations[0]=(struct koa2_range){0x81400000u,0xd000u};
    p.fdt_reservations[1]=(struct koa2_range){0x81500000u,271689u};
    p.fdt_reservations[2]=(struct koa2_range){0x9be00000u,0x80000u};
    p.fdt_reservation_count=3;
    assert(koa2_plan_validate(&p)==0);
    q=p; q.fdt_reservations[0].size=0xe000u; assert(koa2_plan_validate(&q)==KOA2_RANGE);
    /* ... but not the stock FDT or stock initrd it was copied from. */
    q=p; q.fdt_reservations[3]=(struct koa2_range){0x9d681000u,0xd000u}; q.fdt_reservation_count=4;
    assert(koa2_plan_validate(&q)==KOA2_RANGE);
    q=p; q.fdt_reservations[3]=(struct koa2_range){0x9d691000u,0x55aa00u}; q.fdt_reservation_count=4;
    assert(koa2_plan_validate(&q)==KOA2_RANGE);
    q=p; q.fdt_reservations[3]=(struct koa2_range){0x80100000u,0x1000u}; q.fdt_reservation_count=4;
    assert(koa2_plan_validate(&q)==KOA2_RANGE);
    /* Without an initramfs. */
    q=base(); assert(koa2_plan_propose(&q,51008,0)==0 && q.segment_count==2 && koa2_plan_validate(&q)==0);
    /* zImage inside the kernel it decompresses: head.S would relocate itself. */
    q=base(); q.segments[0].dest=0x80800000u; assert(koa2_plan_validate(&q)==KOA2_RANGE);
    /* zImage above 0x88000000: AUTO_ZRELADDR would put the kernel at 0x88008000. */
    q=base(); q.segments[0].dest=0x88d00000u; assert(koa2_plan_validate(&q)==KOA2_IDENTITY);
    /* Even with a matching footprint: RAM below 0x88000000 would be lost. */
    q.footprint.zreladdr=0x88008000u; q.footprint.kernel_end=0x88c55a0cu; assert(koa2_plan_validate(&q)==KOA2_RANGE);
    /* Wrong zImage size, runtime smaller than the file, footprint mismatch. */
    q=base(); q.segments[0].size--; assert(koa2_plan_validate(&q)==KOA2_IDENTITY);
    q=base(); q.footprint.zimage_runtime=1; assert(koa2_plan_validate(&q)==KOA2_IDENTITY);
    q=base(); q.footprint.kernel_end=0x80008000u; assert(koa2_plan_validate(&q)==KOA2_IDENTITY);
    /* Unaligned, outside RAM, in the EPDC hole, on a persistent range. */
    q=base(); q.segments[2].dest+=0x10; assert(koa2_plan_validate(&q)==KOA2_RANGE);
    q=base(); q.segments[2].dest=0xa0000000u; assert(koa2_plan_validate(&q)==KOA2_RANGE);
    q=base(); q.segments[2].dest=0x9ff00000u; assert(koa2_plan_validate(&q)==0);
    q=base(); q.segments[2].dest=0x9ff80000u; assert(koa2_plan_validate(&q)==KOA2_RANGE); /* Falcon BIOS */
    q=base(); q.segments[2].dest=0x9c100000u; assert(koa2_plan_validate(&q)==KOA2_RANGE);
    q=base(); q.segments[2].dest=0x9bfff000u; assert(koa2_plan_validate(&q)==KOA2_RANGE);
    q=base(); q.segments[2].dest=0x9be7f000u; assert(koa2_plan_validate(&q)==KOA2_RANGE);
    q=base(); q.segments[2].dest=0x9fe00000u; q.segments[2].size=0x200000u; assert(koa2_plan_validate(&q)==KOA2_RANGE);
    /* Overwriting the dead stock FDT and initrd is allowed. */
    q=base(); q.segments[2].dest=0x9d691000u; assert(koa2_plan_validate(&q)==0);
    /* FDT on the running zImage, on the kernel BSS, too big, across 2 MiB of sections. */
    q=base(); q.segments[1].dest=0x81300000u; assert(koa2_plan_validate(&q)==KOA2_RANGE);
    q=base(); q.segments[1].dest=0x80c55000u; assert(koa2_plan_validate(&q)==KOA2_RANGE);
    q=base(); q.segments[1].size=2*MIB+1; assert(koa2_plan_validate(&q)==KOA2_RANGE);
    q=base(); q.segments[1].dest=0x814ff000u; q.segments[1].size=MIB+0x2000u;
    q.segments[2].dest=0x81700000u; assert(koa2_plan_validate(&q)==KOA2_RANGE);
    q.segments[1].size=MIB+0x1000u; assert(koa2_plan_validate(&q)==0);
    /* initramfs over FDT, over the page directory, over the running zImage. */
    q=base(); q.segments[2].dest=0x81400000u; assert(koa2_plan_validate(&q)==KOA2_RANGE);
    q=base(); q.segments[2].dest=0x80004000u; assert(koa2_plan_validate(&q)==KOA2_RANGE);
    q=base(); q.segments[2].dest=0x81000000u; assert(koa2_plan_validate(&q)==KOA2_RANGE);
    /* Segment counts and kinds. */
    q=base(); q.segments[2].kind=KOA2_SEG_FDT; assert(koa2_plan_validate(&q)==KOA2_FORMAT);
    q=base(); q.segments[2].kind=0; assert(koa2_plan_validate(&q)==KOA2_FORMAT);
    q=base(); q.segment_count=1; assert(koa2_plan_validate(&q)==KOA2_FORMAT);
    q=base(); q.segment_count=0; assert(koa2_plan_validate(&q)==KOA2_BOUNDS);
    q=base(); q.segment_count=4; assert(koa2_plan_validate(&q)==KOA2_BOUNDS);
    /* RAM beyond lowmem, missing tables, overflow. */
    q=base(); q.lowmem_end=0x90000000u; assert(koa2_plan_validate(&q)==KOA2_RANGE);
    q=base(); q.ram=NULL; assert(koa2_plan_validate(&q)==KOA2_BOUNDS);
    q=base(); q.persistent=NULL; assert(koa2_plan_validate(&q)==KOA2_BOUNDS);
    q=base(); q.segments[2].size=UINT64_MAX; assert(koa2_plan_validate(&q)==KOA2_RANGE);
    q=base(); q.fdt_reservation_count=KOA2_PLAN_MAX_RESERVATIONS+1; assert(koa2_plan_validate(&q)==KOA2_BOUNDS);
    assert(koa2_plan_validate(NULL)==KOA2_BOUNDS);
    assert(koa2_plan_propose(NULL,1,0)==KOA2_BOUNDS);
    q=base(); assert(koa2_plan_propose(&q,0,0)==KOA2_BOUNDS && koa2_plan_propose(&q,2*MIB+1,0)==KOA2_BOUNDS);
    /* A bigger initramfs (a real Android ramdisk) still fits below the printk buffer. */
    q=base(); assert(koa2_plan_propose(&q,51008,64u*MIB)==0 && koa2_plan_validate(&q)==0);
    /* The largest that fits ends at the printk buffer, 0x9be00000. */
    q=base(); assert(koa2_plan_propose(&q,51008,0x9be00000u-0x81500000u)==0 && koa2_plan_validate(&q)==0);
    q=base(); assert(koa2_plan_propose(&q,51008,0x9be00000u-0x81500000u+1)==0 && koa2_plan_validate(&q)==KOA2_RANGE);
    puts("KOA2 plan: measured footprint layout, zreladdr window, relocation, persistent ranges, FDT sections, reserve map passed");
    return 0;
}
