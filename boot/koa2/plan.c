/* SPDX-License-Identifier: GPL-2.0-only */
#include "plan.h"
#define PAGE 0x1000u
#define MIB 0x100000u
#define TEXT_OFFSET 0x8000u
#define PAGE_DIR 0x4000u
#define ZRELADDR_MASK 0xf8000000u
static struct koa2_range rng(uint64_t start, uint64_t size) { struct koa2_range r={start,size}; return r; }
static int inside(struct koa2_range in, struct koa2_range out) {
    return in.start>=out.start && in.size<=out.size && in.start-out.start<=out.size-in.size;
}
static int in_ram(struct koa2_range r, const struct koa2_plan *p) {
    size_t i;
    for(i=0;i<p->ram_count;i++) if(inside(r,p->ram[i])) return 1;
    return 0;
}
static int hits(struct koa2_range r, const struct koa2_range *x, size_t n) {
    size_t i;
    for(i=0;i<n;i++) if(koa2_range_overlap(r,x[i])) return 1;
    return 0;
}
static const struct koa2_segment *find(const struct koa2_plan *p, enum koa2_segment_kind kind, int *count) {
    const struct koa2_segment *found=NULL;
    size_t i;
    *count=0;
    for(i=0;i<p->segment_count;i++) if(p->segments[i].kind==kind) { found=&p->segments[i]; ++*count; }
    return found;
}
/* What the decompressor and early kernel write: page directory through BSS. */
static struct koa2_range kernel_writes(const struct koa2_footprint *f) {
    uint64_t end=(f->kernel_end+PAGE-1)&~(uint64_t)(PAGE-1);
    return rng(f->zreladdr-PAGE_DIR, end-(f->zreladdr-PAGE_DIR));
}
int koa2_plan_validate(const struct koa2_plan *p) {
    const struct koa2_footprint *f;
    const struct koa2_segment *z,*d,*r;
    struct koa2_range kernel,zrun,dtb,rd={0,0},held[KOA2_PLAN_MAX_SEGMENTS];
    int nz,nd,nr;
    size_t i,j;
    if(!p || !p->ram || !p->ram_count || (p->persistent_count && !p->persistent) ||
       !p->segment_count || p->segment_count>KOA2_PLAN_MAX_SEGMENTS ||
       p->fdt_reservation_count>KOA2_PLAN_MAX_RESERVATIONS) return KOA2_BOUNDS;
    for(i=0;i<p->ram_count;i++)
        if(!koa2_range_valid(p->ram[i]) || p->ram[i].start+p->ram[i].size>p->lowmem_end) return KOA2_RANGE;
    for(i=0;i<p->persistent_count;i++) if(!koa2_range_valid(p->persistent[i])) return KOA2_RANGE;
    for(i=0;i<p->segment_count;i++) {
        const struct koa2_segment *s=&p->segments[i];
        struct koa2_range at=rng(s->dest,s->size);
        if(s->kind<KOA2_SEG_ZIMAGE || s->kind>KOA2_SEG_INITRAMFS) return KOA2_FORMAT;
        /* Rule 1: page aligned, inside one measured RAM interval (all of it
         * lowmem), and clear of every range that must outlive the stock kernel. */
        if((s->dest&(PAGE-1)) || !koa2_range_valid(at)) return KOA2_RANGE;
        if(!in_ram(at,p) || hits(at,p->persistent,p->persistent_count)) return KOA2_RANGE;
    }
    z=find(p,KOA2_SEG_ZIMAGE,&nz); d=find(p,KOA2_SEG_FDT,&nd); r=find(p,KOA2_SEG_INITRAMFS,&nr);
    /* Rule 2: exactly one zImage and one FDT, at most one initramfs. */
    if(nz!=1 || nd!=1 || nr>1) return KOA2_FORMAT;
    f=&p->footprint;
    /* Rule 3: the footprint is the one AUTO_ZRELADDR derives from this load
     * address, and it describes this zImage. */
    if(f->zreladdr!=(z->dest&ZRELADDR_MASK)+TEXT_OFFSET || f->kernel_end<=f->zreladdr ||
       z->size!=f->zimage_size || f->zimage_runtime<f->zimage_size) return KOA2_IDENTITY;
    /* Rule 3b: the zImage sits in the first 128 MiB of RAM. Above that,
     * AUTO_ZRELADDR and ARM_PATCH_PHYS_VIRT make the new kernel take its own
     * 128 MiB-aligned base as PHYS_OFFSET and lose all RAM below it, the FDT
     * and initramfs with it if they lie there. */
    {
        uint64_t low=UINT64_MAX;
        for(i=0;i<p->ram_count;i++) if(p->ram[i].start<low) low=p->ram[i].start;
        if((z->dest&ZRELADDR_MASK)!=low) return KOA2_RANGE;
    }
    kernel=kernel_writes(f);
    zrun=rng(z->dest,f->zimage_runtime);
    /* Rule 4: the running zImage (with BSS, stack, malloc) lies above all it
     * decompresses, so head.S never relocates itself and never writes over its
     * own code; both stay inside RAM and off persistent ranges. */
    if(!koa2_range_valid(kernel) || !koa2_range_valid(zrun) || z->dest<kernel.start+kernel.size ||
       !in_ram(kernel,p) || hits(kernel,p->persistent,p->persistent_count) ||
       !in_ram(zrun,p) || hits(zrun,p->persistent,p->persistent_count)) return KOA2_RANGE;
    /* Rule 5: the FDT is clear of the kernel and the running zImage, and fits
     * the two 1 MiB sections head.S maps at its address before paging_init. */
    dtb=rng(d->dest,d->size);
    if(koa2_range_overlap(dtb,kernel) || koa2_range_overlap(dtb,zrun) || d->size>2*MIB ||
       dtb.start+dtb.size>(dtb.start&~(uint64_t)(MIB-1))+2*MIB) return KOA2_RANGE;
    /* Rule 6: the initramfs is clear of the kernel, the zImage and the FDT. */
    if(r) {
        rd=rng(r->dest,r->size);
        if(koa2_range_overlap(rd,kernel) || koa2_range_overlap(rd,zrun) || koa2_range_overlap(rd,dtb)) return KOA2_RANGE;
    }
    /* Rule 7: no two segments overlap. */
    for(i=0;i<p->segment_count;i++) held[i]=rng(p->segments[i].dest,p->segments[i].size);
    for(i=0;i<p->segment_count;i++) for(j=i+1;j<p->segment_count;j++)
        if(koa2_range_overlap(held[i],held[j])) return KOA2_RANGE;
    /* Rule 8: every reservation in the FDT handed over describes something
     * real after the jump: a persistent range, the FDT itself or the
     * initramfs. A stale entry (the stock FDT or initrd) reserves dead memory
     * or, worse, makes arm_memblock_init drop the initramfs as overlapping. */
    for(i=0;i<p->fdt_reservation_count;i++) {
        struct koa2_range x=p->fdt_reservations[i];
        int ok=0;
        if(!koa2_range_valid(x)) return KOA2_RANGE;
        for(j=0;j<p->persistent_count;j++) if(inside(x,p->persistent[j])) ok=1;
        /* U-Boot reserves the FDT rounded up to a page; segments start on
         * pages, so the FDT's last page holds nothing else. */
        if(inside(x,rng(dtb.start,(dtb.size+PAGE-1)&~(uint64_t)(PAGE-1))) || (r && inside(x,rd))) ok=1;
        if(!ok) return KOA2_RANGE;
    }
    return KOA2_OK;
}
const struct koa2_range koa2_handoff_persistent[KOA2_HANDOFF_PERSISTENT_COUNT]={
    {0x9be00000u,0x00080000u}, /* Lab126 printk buffer and previous-boot copy */
    {0x9bf00000u,0x00100000u}, /* ramoops */
    {0x9c000000u,0x00500000u}, /* epdc_reserved, no-map */
    {0x9ffc0000u,0x00040000u}, /* Falcon storage BIOS + BIOS */
};
static uint64_t mib_up(uint64_t v) { return (v+MIB-1)&~(uint64_t)(MIB-1); }
int koa2_plan_propose(struct koa2_plan *p, uint64_t fdt_size, uint64_t initramfs_size) {
    uint64_t at;
    if(!p || !fdt_size || fdt_size>2*MIB || p->footprint.kernel_end>UINT32_MAX ||
       p->footprint.zimage_runtime>UINT32_MAX || initramfs_size>UINT32_MAX) return KOA2_BOUNDS;
    at=mib_up(p->footprint.kernel_end);
    p->segments[0].kind=KOA2_SEG_ZIMAGE; p->segments[0].dest=at; p->segments[0].size=p->footprint.zimage_size;
    at=mib_up(at+p->footprint.zimage_runtime);
    p->segments[1].kind=KOA2_SEG_FDT; p->segments[1].dest=at; p->segments[1].size=fdt_size;
    p->segment_count=2;
    if(initramfs_size) {
        at=mib_up(at+fdt_size);
        p->segments[2].kind=KOA2_SEG_INITRAMFS; p->segments[2].dest=at; p->segments[2].size=initramfs_size;
        p->segment_count=3;
    }
    return KOA2_OK;
}
