/* SPDX-License-Identifier: GPL-2.0-only */
/* KOA2 handoff staging module. It stages, in pages it owns, the new zImage, the
 * stock runtime FDT (read by userspace from /sys/firmware/fdt) and the
 * initramfs; checks the zImage against what the build pinned; rewrites the FDT
 * for a layout derived from the measured memory map; and validates that layout.
 *
 * It has no jump. There is no ioctl that copies a payload to its destination,
 * stops a device, takes down the second core or leaves Linux. Everything it
 * does is reversible by closing the device: the staged pages are freed and the
 * stock system is untouched. The copy, quiesce and jump are a later stage and
 * will be gated behind a separate device the way PW3's native-PM dry-run is.
 *
 * The three checkers (contract.c, plan.c, fdt-rewrite.c) are the same code the
 * host tests exercise; koa2-types.h lets them build here without libc. */
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <linux/capability.h>
#include <linux/uaccess.h>
#include <linux/utsname.h>
#include <asm/memory.h>
#include "stage-abi.h"
#include "contract.h"
#include "plan.h"
#include "fdt-rewrite.h"
#include "copylist.h"
#include "sha256.h"
#include "target-lock.h"

#if !defined(CONFIG_ARM) || PAGE_SHIFT != 12
#error "KOA2 handoff staging is ARM32 with 4 KiB pages only"
#endif
/* SMP is fine while staging: nothing here touches another core. The jump stage,
 * which is not in this file, is what needs the second core down. */

#define K2S_FDT_MAX (64u * 1024u)
#define K2S_FDT_OUT_MAX (K2S_FDT_MAX + 8192u)
#define MAX_WRITE (64u * 1024u)
/* One list page per 1023 entries; the largest payload here is about 108000
 * pages, so 128 is far more than enough and bounds the allocation. */
#define MAX_LIST_PAGES 128u
/* The largest the three payloads can be: a zImage below 8 MiB, the FDT, and an
 * initramfs that reaches the printk buffer from 0x81500000. */
#define MAX_PAYLOAD_BYTES (8u*1024u*1024u + K2S_FDT_MAX + 0x1a900000u)
#define MAX_PAYLOAD_PAGES (MAX_PAYLOAD_BYTES / PAGE_SIZE + K2S_SLOTS)
/* Pages the allocator offers in the zImage window or a persistent range are
 * held, not used, so it cannot offer them again. On a running stock system
 * that window is 128 MiB of mostly-used memory, and every page held is one the
 * stock system cannot have back until we close, so the number held is capped
 * well below what would push it towards the OOM killer: 32 MiB. Hitting the
 * cap fails BEGIN with -ENOSPC (the allocator running dry is -ENOMEM), and
 * closing gives everything back. */
#define MAX_COLLISIONS 8192u

static const k2s_u8 pinned_kernel_sha[32] = K2S_KERNEL_SHA256;

struct allocation { struct page *page; };
struct slot { unsigned int bytes, written, first, pages; k2s_u8 expected[32]; };
struct store {
    struct mutex lock;
    unsigned int state;
    struct slot slot[K2S_SLOTS];
    struct allocation *owned, *rejected;
    unsigned int count, collisions;
    int seal_stage, seal_result;
    unsigned int fdt_bytes, plan_valid;
    unsigned int list_first, list_pages, list_entries;   /* the audited copy list */
    struct koa2_plan plan;         /* filled by seal(); no addresses leave the module */
};
static atomic_t opened = ATOMIC_INIT(0);
/* Every import has to be one some stock module also imports, so that its CRC
 * can be checked before loading (ports/koa2/module-crcs.py). That rules out
 * __vmalloc (with PAGE_KERNEL), PHYS_OFFSET and page_to_pfn under
 * ARM_PATCH_PHYS_VIRT (__pv_phys_pfn_offset), and ulong parameters. */
static unsigned int live_pages;
module_param(live_pages, uint, 0444);

static void wipe(void *p, size_t n) { volatile u8 *v=p; while(n--) *v++=0; }
/* All RAM is lowmem here, so a page's linear address gives its physical one
 * through the patched virt_to_phys, which imports nothing. */
static u32 phys_of(struct page *page) { return (u32)virt_to_phys(page_address(page)); }
static u32 owned_phys(struct store *s, unsigned int i) { return phys_of(s->owned[i].page); }
static void *owned_addr(struct store *s, unsigned int i) { return page_address(s->owned[i].page); }

static void free_record(struct allocation *a)
{
    if(a->page) { wipe(page_address(a->page),PAGE_SIZE); __free_page(a->page); live_pages--; a->page=NULL; }
}
static void discard(struct store *s)
{
    unsigned int i;
    for(i=0;i<s->count;++i) free_record(&s->owned[i]);
    for(i=0;i<s->collisions;++i) free_record(&s->rejected[i]);
    vfree(s->owned); vfree(s->rejected);
    s->owned=s->rejected=NULL; s->count=s->collisions=0;
    wipe(s->slot,sizeof(s->slot)); memset(&s->plan,0,sizeof(s->plan));
    s->fdt_bytes=s->plan_valid=0; s->state=K2S_EMPTY;
    s->list_first=s->list_pages=s->list_entries=0;
}
/* The ranges a staged page must avoid: the running stock kernel's low memory
 * through 0x88000000 would hold the new kernel and its decompression, and the
 * persistent ranges must survive the jump. We keep source pages out of both,
 * so the later copy stage never has a source sitting on a destination. */
static int page_usable(u32 base)
{
    struct koa2_range page = { base, PAGE_SIZE };
    unsigned int i;
    struct koa2_range window = { 0x80000000u, 0x08000000u };  /* zImage load window */
    if(koa2_range_overlap(page,window)) return 0;
    for(i=0;i<KOA2_HANDOFF_PERSISTENT_COUNT;++i)
        if(koa2_range_overlap(page,koa2_handoff_persistent[i])) return 0;
    /* Inside one measured RAM interval. */
    for(i=0;i<KOA2_MEASURED_RAM_COUNT;++i) {
        struct koa2_range r=koa2_measured_ram[i];
        if(base>=r.start && base-r.start<=r.size-PAGE_SIZE) return 1;
    }
    return 0;
}
static int allocate_owned(struct store *s)
{
    struct page *page;
    u32 base;
    for(;;) {
        if(s->collisions>=MAX_COLLISIONS) return -ENOSPC;
        page=alloc_page(GFP_KERNEL|__GFP_ZERO|__GFP_NORETRY|__GFP_NOWARN);
        if(!page) return -ENOMEM;
        live_pages++;
        base=phys_of(page);
        if(page_usable(base)) break;
        s->rejected[s->collisions++].page=page;
    }
    s->owned[s->count++].page=page;
    return 0;
}

static int begin(struct store *s, void __user *arg)
{
    struct k2s_begin b;
    unsigned int i,j,pages=0;
    int rc;
    if(s->state!=K2S_EMPTY) return -EBUSY;
    if(copy_from_user(&b,arg,sizeof(b))) return -EFAULT;
    if(b.version!=K2S_VERSION || b.reserved) return -EINVAL;
    if(b.images[0].bytes<48 || b.images[0].bytes>8u*1024u*1024u ||
       b.images[1].bytes<48 || b.images[1].bytes>K2S_FDT_MAX ||
       !b.images[2].bytes || b.images[2].bytes>0x1a900000u) return -EINVAL;
    /* Only the build's own kernel: refuse before allocating anything. */
    if(b.images[0].bytes!=K2S_KERNEL_BYTES || memcmp(b.images[0].sha256,pinned_kernel_sha,32)) return -EKEYREJECTED;
    for(i=0;i<K2S_SLOTS;++i) pages+=DIV_ROUND_UP(b.images[i].bytes,PAGE_SIZE);
    if(pages>MAX_PAYLOAD_PAGES) return -E2BIG;
    s->owned=vzalloc(MAX_PAYLOAD_PAGES*sizeof(*s->owned));
    s->rejected=vzalloc(MAX_COLLISIONS*sizeof(*s->rejected));
    if(!s->owned || !s->rejected) { rc=-ENOMEM; goto fail; }
    for(i=0;i<K2S_SLOTS;++i) {
        struct slot *slot=&s->slot[i];
        slot->bytes=b.images[i].bytes;
        slot->pages=DIV_ROUND_UP(slot->bytes,PAGE_SIZE);
        memcpy(slot->expected,b.images[i].sha256,32);
    }
    for(i=0;i<K2S_SLOTS;++i) {
        s->slot[i].first=s->count;
        for(j=0;j<s->slot[i].pages;++j) { rc=allocate_owned(s); if(rc) goto fail; }
    }
    s->state=K2S_WRITING; s->seal_stage=s->seal_result=0;
    wipe(&b,sizeof(b)); return 0;
fail:
    discard(s); wipe(&b,sizeof(b)); return rc;
}
static int put(struct store *s, void __user *arg)
{
    struct k2s_put p;
    struct slot *slot;
    unsigned int done=0;
    if(s->state!=K2S_WRITING) return -EPERM;
    if(copy_from_user(&p,arg,sizeof(p))) return -EFAULT;
    if(p.slot>=K2S_SLOTS || p.reserved || !p.bytes || p.bytes>MAX_WRITE ||
       p.user_buffer>0xffffffffull || p.user_buffer+p.bytes>0x100000000ull) return -EINVAL;
    slot=&s->slot[p.slot];
    if(p.offset!=slot->written || p.bytes>slot->bytes-slot->written) return -EINVAL;
    while(done<p.bytes) {
        unsigned int offset=slot->written+done;
        unsigned int take=min_t(unsigned int,p.bytes-done,PAGE_SIZE-(offset&(PAGE_SIZE-1)));
        u8 *dest=owned_addr(s,slot->first+offset/PAGE_SIZE);
        if(copy_from_user(dest+(offset&(PAGE_SIZE-1)),
              (void __user *)(unsigned long)(p.user_buffer+done),take)) { discard(s); return -EFAULT; }
        done+=take;
    }
    slot->written+=done; return 0;
}

/* Copies slot bytes [offset, offset+n) out of the owned pages. */
static void slot_read(struct store *s, unsigned int slot, unsigned int offset, u8 *out, unsigned int n)
{
    while(n) {
        unsigned int take=min_t(unsigned int,n,PAGE_SIZE-(offset&(PAGE_SIZE-1)));
        memcpy(out,(u8 *)owned_addr(s,s->slot[slot].first+offset/PAGE_SIZE)+(offset&(PAGE_SIZE-1)),take);
        out+=take; offset+=take; n-=take;
    }
}
/* The staged pages of one segment, as physical addresses in order. */
static int segment_pages(struct store *s, unsigned int slot, unsigned int count, u32 *out)
{
    unsigned int i;
    if(count>s->slot[slot].pages) return KOA2_BOUNDS;
    for(i=0;i<count;++i) out[i]=owned_phys(s,s->slot[slot].first+i);
    return KOA2_OK;
}
/* Allocates the list pages, writes the list, and audits it against the plan.
 * The audit is what matters: it refuses a list whose own pages or sources lie
 * where the copy writes, which is what makes the copy order irrelevant. */
static int build_copy_list(struct store *s)
{
    struct koa2_copy_segment segments[KOA2_PLAN_MAX_SEGMENTS];
    u32 *pages[KOA2_PLAN_MAX_SEGMENTS]={NULL,NULL,NULL};
    u32 **list_virtual=NULL, *list_physical=NULL;
    size_t want,entries=0,audited=0,i;
    int result=KOA2_BOUNDS;
    if(s->plan.segment_count>KOA2_PLAN_MAX_SEGMENTS) return KOA2_BOUNDS;
    for(i=0;i<s->plan.segment_count;++i) {
        unsigned int count=DIV_ROUND_UP((unsigned int)s->plan.segments[i].size,PAGE_SIZE);
        pages[i]=vzalloc(count*sizeof(u32));
        if(!pages[i] || segment_pages(s,(unsigned int)i,count,pages[i])) goto out;
        segments[i].dest=s->plan.segments[i].dest;
        segments[i].pages=pages[i];
        segments[i].page_count=count;
    }
    want=koa2_copylist_pages(segments,s->plan.segment_count);
    if(!want || want>MAX_LIST_PAGES) goto out;
    list_virtual=vzalloc(want*sizeof(*list_virtual));
    list_physical=vzalloc(want*sizeof(*list_physical));
    if(!list_virtual || !list_physical) goto out;
    s->list_first=s->count;
    for(i=0;i<want;++i) {
        if(allocate_owned(s)) goto out;
        list_virtual[i]=owned_addr(s,s->list_first+(unsigned int)i);
        list_physical[i]=owned_phys(s,s->list_first+(unsigned int)i);
    }
    result=koa2_copylist_build(list_virtual,list_physical,want,segments,s->plan.segment_count,&entries);
    if(result) goto out;
    result=koa2_copylist_audit((const u32 *const *)list_virtual,list_physical,want,
                               segments,s->plan.segment_count,
                               koa2_handoff_persistent,KOA2_HANDOFF_PERSISTENT_COUNT,&audited,NULL);
    if(result) goto out;
    if(audited!=entries) { result=KOA2_BOUNDS; goto out; }
    s->list_pages=(unsigned int)want; s->list_entries=(unsigned int)entries;
    result=KOA2_OK;
out:
    for(i=0;i<KOA2_PLAN_MAX_SEGMENTS;++i) vfree(pages[i]);
    vfree(list_virtual); vfree(list_physical);
    return result;
}
static int seal_fail(struct store *s, int stage, int result, int rc)
{
    s->seal_stage=stage; s->seal_result=result;
    discard(s);
    return rc;
}
static int seal(struct store *s)
{
    struct koa2_fdt_edit edit;
    struct koa2_fdt_seen seen;
    SHA256_CTX ctx;
    u8 hash[32], *fdt=NULL, *out=NULL;
    size_t first=0, bytes=0;
    unsigned int i,j;
    int result;
    if(s->state!=K2S_WRITING) return -EPERM;
    for(i=0;i<K2S_SLOTS;++i) if(s->slot[i].written!=s->slot[i].bytes) return seal_fail(s,K2S_STAGE_INCOMPLETE,0,-ENODATA);
    /* Every slot is what the caller declared, and the zImage is the build's. */
    for(i=0;i<K2S_SLOTS;++i) {
        unsigned int done=0;
        sha256_init(&ctx);
        for(j=0;j<s->slot[i].pages;++j) {
            unsigned int take=min_t(unsigned int,s->slot[i].bytes-done,PAGE_SIZE);
            sha256_update(&ctx,owned_addr(s,s->slot[i].first+j),take);
            done+=take;
        }
        sha256_final(&ctx,hash);
        if(memcmp(hash,s->slot[i].expected,32)) { wipe(&ctx,sizeof(ctx)); return seal_fail(s,K2S_STAGE_HASH,(int)i,-EBADMSG); }
    }
    wipe(&ctx,sizeof(ctx));
    if(s->slot[0].bytes!=K2S_KERNEL_BYTES ||
       memcmp(s->slot[0].expected,pinned_kernel_sha,32)) return seal_fail(s,K2S_STAGE_PINNED,0,-EKEYREJECTED);
    fdt=kmalloc(K2S_FDT_MAX,GFP_KERNEL|__GFP_NOWARN);
    out=kzalloc(K2S_FDT_OUT_MAX,GFP_KERNEL|__GFP_NOWARN);
    if(!fdt || !out) { kfree(fdt); kfree(out); return seal_fail(s,K2S_STAGE_FDT,0,-ENOMEM); }
    slot_read(s,1,0,fdt,s->slot[1].bytes);
    /* The stock FDT: Zelda, and every reservation in it is a measured exclusion. */
    result=koa2_fdt_reservations(fdt,s->slot[1].bytes,s->plan.fdt_reservations,
                                 KOA2_PLAN_MAX_RESERVATIONS,&s->plan.fdt_reservation_count);
    if(result) { result=seal_fail(s,K2S_STAGE_FDT,result,-ENOEXEC); goto out; }
    if(koa2_reservations_covered(s->plan.fdt_reservations,s->plan.fdt_reservation_count,
                                 koa2_measured_excluded,KOA2_MEASURED_EXCLUDED_COUNT)) {
        result=seal_fail(s,K2S_STAGE_FDT_RESERVATIONS,0,-ENOEXEC); goto out;
    }
    /* Propose from the measured map and the build's footprint, then rewrite
     * the FDT for it. The first rewrite only measures the result's size, which
     * the initrd values do not change (always one cell each). */
    s->plan.footprint.zreladdr=K2S_ZRELADDR;
    s->plan.footprint.kernel_end=K2S_KERNEL_END;
    s->plan.footprint.zimage_size=K2S_KERNEL_BYTES;
    s->plan.footprint.zimage_runtime=K2S_ZIMAGE_RUNTIME;
    s->plan.ram=koa2_measured_ram; s->plan.ram_count=KOA2_MEASURED_RAM_COUNT;
    s->plan.persistent=koa2_handoff_persistent; s->plan.persistent_count=KOA2_HANDOFF_PERSISTENT_COUNT;
    s->plan.lowmem_end=KOA2_LOWMEM_END;
    memset(&edit,0,sizeof(edit)); edit.bootargs=K2S_BOOTARGS;
    if(K2S_HAS_INITRAMFS) { edit.initrd_start=0x1000; edit.initrd_end=0x2000; }
    result=koa2_fdt_rewrite(fdt,s->slot[1].bytes,&edit,out,K2S_FDT_OUT_MAX,&first,&seen);
    if(result || koa2_plan_propose(&s->plan,first,K2S_HAS_INITRAMFS?s->slot[2].bytes:0)) {
        result=seal_fail(s,K2S_STAGE_REWRITE,result,-ENOEXEC); goto out;
    }
    if(K2S_HAS_INITRAMFS) {
        edit.initrd_start=s->plan.segments[2].dest;
        edit.initrd_end=s->plan.segments[2].dest+s->slot[2].bytes;
    }
    result=koa2_fdt_rewrite(fdt,s->slot[1].bytes,&edit,out,K2S_FDT_OUT_MAX,&bytes,&seen);
    if(result || bytes!=first) { result=seal_fail(s,K2S_STAGE_REWRITE,result,-ENOEXEC); goto out; }
    /* /memory is passed on as U-Boot wrote it; the EPDC no-map node makes the hole. */
    if(seen.memory_regs!=1 || seen.memory_start!=0x80000000u || seen.memory_size!=0x20000000u) {
        result=seal_fail(s,K2S_STAGE_MEMORY,0,-ENOEXEC); goto out;
    }
    result=koa2_fdt_reservations(out,bytes,s->plan.fdt_reservations,KOA2_PLAN_MAX_RESERVATIONS,
                                 &s->plan.fdt_reservation_count);
    if(!result) result=koa2_plan_validate(&s->plan);
    if(result) { result=seal_fail(s,K2S_STAGE_PLAN,result,-ENOEXEC); goto out; }
    /* No staged source page may lie on a destination: the copy stage reads
     * them while it writes there. page_usable() keeps them out of the zImage
     * window, but a large initramfs reaches past it. */
    for(i=0;i<s->count;++i) {
        struct koa2_range page={owned_phys(s,i),PAGE_SIZE};
        for(j=0;j<s->plan.segment_count;++j) {
            struct koa2_range dest={s->plan.segments[j].dest,s->plan.segments[j].size};
            if(koa2_range_overlap(page,dest)) { result=seal_fail(s,K2S_STAGE_PAGES,(int)j,-EADDRINUSE); goto out; }
        }
    }
    /* The rewritten FDT replaces the stock one in its slot: same pages if it
     * fits, which it does because the rewrite only drops memreserves and
     * NOPs and the new bootargs are no longer than COMMAND_LINE_SIZE. */
    if(DIV_ROUND_UP(bytes,PAGE_SIZE)>s->slot[1].pages) { result=seal_fail(s,K2S_STAGE_REWRITE,0,-EFBIG); goto out; }
    for(i=0;i<s->slot[1].pages;++i) {
        unsigned int at=i*PAGE_SIZE;
        u8 *page=owned_addr(s,s->slot[1].first+i);
        memset(page,0,PAGE_SIZE);
        if(at<bytes) memcpy(page,out+at,min_t(size_t,PAGE_SIZE,bytes-at));
    }
    /* Build and audit the list the jump stage would walk. Nothing here walks
     * it: this is the last thing that can be checked without leaving Linux, so
     * it is checked now, on the device, rather than assumed later. */
    result=build_copy_list(s);
    if(result) { result=seal_fail(s,K2S_STAGE_COPYLIST,result,-EUCLEAN); goto out; }
    s->fdt_bytes=(unsigned int)bytes; s->plan_valid=1; s->state=K2S_SEALED;
    s->seal_stage=s->seal_result=0; result=0;
out:
    wipe(fdt,K2S_FDT_MAX); wipe(out,K2S_FDT_OUT_MAX); kfree(fdt); kfree(out);
    return result;
}

static long stage_ioctl(struct file *file, unsigned int command, unsigned long arg)
{
    struct store *s=file->private_data;
    struct k2s_status status;
    unsigned int i;
    int rc;
    if(!capable(CAP_SYS_BOOT)) return -EPERM;
    if(mutex_lock_interruptible(&s->lock)) return -EINTR;
    switch(command) {
    case K2S_BEGIN: rc=begin(s,(void __user *)arg); break;
    case K2S_PUT: rc=put(s,(void __user *)arg); break;
    case K2S_SEAL: rc=arg?-EINVAL:seal(s); break;
    case K2S_DISCARD: if(arg) { rc=-EINVAL; break; } discard(s); rc=0; break;
    case K2S_STATUS:
        memset(&status,0,sizeof(status));
        status.version=K2S_VERSION; status.state=s->state;
        for(i=0;i<K2S_SLOTS;++i) {
            status.written[i]=s->slot[i].written; status.sizes[i]=s->slot[i].bytes;
            status.payload_pages+=s->slot[i].pages;
        }
        status.collision_pages=s->collisions; status.fdt_bytes=s->fdt_bytes;
        status.list_pages=s->list_pages; status.list_entries=s->list_entries;
        status.seal_stage=s->seal_stage; status.seal_result=s->seal_result;
        status.plan_valid=s->plan_valid; status.execute_available=0;
        rc=copy_to_user((void __user *)arg,&status,sizeof(status))?-EFAULT:0; break;
    default: rc=-ENOTTY;
    }
    mutex_unlock(&s->lock);
    return rc;
}
static int stage_open(struct inode *inode, struct file *file)
{
    struct store *s;
    (void)inode;
    if(!capable(CAP_SYS_BOOT) || !(file->f_mode&FMODE_WRITE)) return -EPERM;
    if(atomic_cmpxchg(&opened,0,1)) return -EBUSY;
    s=kzalloc(sizeof(*s),GFP_KERNEL);
    if(!s) { atomic_set(&opened,0); return -ENOMEM; }
    mutex_init(&s->lock); file->private_data=s;
    return 0;
}
static int stage_release(struct inode *inode, struct file *file)
{
    struct store *s=file->private_data;
    (void)inode;
    discard(s); kfree(s); file->private_data=NULL;
    atomic_set(&opened,0);
    return 0;
}
static const struct file_operations ops={
    .owner=THIS_MODULE, .open=stage_open, .release=stage_release,
    .unlocked_ioctl=stage_ioctl, .llseek=no_llseek,
};
static struct miscdevice device={ .minor=MISC_DYNAMIC_MINOR, .name="koa2-handoff-stage", .fops=&ops, .mode=0600 };
static int __init stage_init(void)
{
    const struct new_utsname *u=init_utsname();
    BUILD_BUG_ON(sizeof(struct k2s_begin)!=116);
    BUILD_BUG_ON(sizeof(struct k2s_put)!=24);
    BUILD_BUG_ON(sizeof(struct k2s_status)!=68);
    BUILD_BUG_ON(sizeof(K2S_BOOTARGS)>KOA2_BOOTARGS_MAX+1);
    /* Only the stock kernel the measurements came from, with the split they assume. */
    if(strcmp(u->release,K2S_STOCK_RELEASE) || strcmp(u->version,K2S_STOCK_VERSION) ||
       strcmp(u->machine,K2S_STOCK_MACHINE) || PAGE_OFFSET!=0x80000000u ||
       virt_to_phys((void *)PAGE_OFFSET)!=0x80000000u) return -ENOEXEC;
    return misc_register(&device);
}
static void __exit stage_exit(void) { misc_deregister(&device); }
module_init(stage_init); module_exit(stage_exit);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("KOA2 handoff staging: stage, check, rewrite FDT, validate plan; no jump");
