/* SPDX-License-Identifier: GPL-2.0-only */
/* The execute module's dry-run, #included at the tail of stage.c when
 * KOA2_EXEC_BUILD is defined. It shares stage.c's store, BEGIN/PUT/SEAL and
 * audited copy list, and adds exactly one reversible step beyond the staging
 * module: it arms the trampoline and reads the CPU state, then stops one step
 * short of disabling the caches (KOA2_Q_CACHES, the point of no return) and
 * puts everything back.
 *
 * It does NOT jump. jump.S is linked in so its code and parameter layout are
 * the real ones, but nothing here branches into it: the control page is filled
 * and then freed. No payload is copied to a destination, no device is stopped
 * by this module, no core is taken down, and execute_available stays 0. The
 * build gate (ports/koa2/build-exec-module.sh) refuses the module if it imports
 * cpu_down/restart or writes a coprocessor register anywhere but inside the
 * trampoline blob. */
#include <linux/cpumask.h>
#include <linux/irqflags.h>
#include "quiesce-verify.h"
#include "jump-params.h"
/* jump.S, linked into this module only (KOA2_JUMP_BUILD). On the host test
 * there is no jump.S; test-exec.c supplies stand-ins and KOA2_EXEC_HOST_BLOB. */
#ifdef __KERNEL__
extern unsigned char koa2_jump_start[], koa2_jump_end[];
#else
extern unsigned char koa2_jump_start[];
#endif

/* Re-audits the copy list the seal already built and stored in owned pages,
 * against segment descriptors rebuilt from the plan. Read-only: it allocates
 * nothing lasting and does not rewrite the list. Mirrors build_copy_list's
 * audit call so the two cannot drift. */
static int exec_reaudit_list(struct store *s, size_t *entries_out)
{
    struct koa2_copy_segment segments[KOA2_PLAN_MAX_SEGMENTS];
    u32 *pages[KOA2_PLAN_MAX_SEGMENTS]={NULL,NULL,NULL};
    const u32 **list_read=NULL; u32 *list_physical=NULL;
    size_t i, audited=0; int result=KOA2_BOUNDS;
    *entries_out=0;
    if(!s->list_pages || s->plan.segment_count>KOA2_PLAN_MAX_SEGMENTS) return KOA2_BOUNDS;
    for(i=0;i<s->plan.segment_count;++i) {
        unsigned int count=DIV_ROUND_UP((unsigned int)s->plan.segments[i].size,PAGE_SIZE);
        pages[i]=vzalloc(count*sizeof(u32));
        if(!pages[i] || segment_pages(s,(unsigned int)i,count,pages[i])) goto out;
        segments[i].dest=s->plan.segments[i].dest;
        segments[i].pages=pages[i]; segments[i].page_count=count;
    }
    list_read=vzalloc(s->list_pages*sizeof(*list_read));
    list_physical=vzalloc(s->list_pages*sizeof(*list_physical));
    if(!list_read || !list_physical) goto out;
    for(i=0;i<s->list_pages;++i) {
        list_read[i]=owned_addr(s,s->list_first+(unsigned int)i);
        list_physical[i]=owned_phys(s,s->list_first+(unsigned int)i);
    }
    result=koa2_copylist_audit(list_read,list_physical,s->list_pages,segments,s->plan.segment_count,
                               koa2_handoff_persistent,KOA2_HANDOFF_PERSISTENT_COUNT,&audited,NULL);
    if(!result) *entries_out=audited;
out:
    for(i=0;i<KOA2_PLAN_MAX_SEGMENTS;++i) vfree(pages[i]);
    vfree(list_read); vfree(list_physical);
    return result;
}
/* The payload pages the trampoline is told to copy: one per page of every
 * segment, the same count build_copy_list emitted as sources. */
static u32 exec_expected_pages(struct store *s)
{
    u32 total=0; unsigned int i;
    for(i=0;i<s->plan.segment_count;++i)
        total+=DIV_ROUND_UP((unsigned int)s->plan.segments[i].size,PAGE_SIZE);
    return total;
}
/* A freshly allocated owned page cannot fall in the zImage window (page_usable
 * keeps it out), so it cannot equal any destination, which all lie there. This
 * confirms that invariant rather than assuming it. */
static int exec_control_clear(struct store *s, u32 control)
{
    struct koa2_range page={control,PAGE_SIZE};
    unsigned int i;
    for(i=0;i<s->plan.segment_count;++i) {
        struct koa2_range dest={s->plan.segments[i].dest,s->plan.segments[i].size};
        if(koa2_range_overlap(page,dest)) return 0;
    }
    return 1;
}
static int exec_dryrun(struct store *s, void __user *arg)
{
    struct k2s_exec_dryrun io;
    struct koa2_quiesce_state qs;
    struct koa2_jump_inputs in;
    struct kj_params params;
    size_t blob, audited=0;
    unsigned long flags;
    u32 cpsr, sctlr=0, ttbcr=0, control=0;
    unsigned int control_index;
    int prc=KOA2_BOUNDS, rc=0;
    if(s->state!=K2S_SEALED || !s->plan_valid) return -EPERM;
    if(s->plan.segment_count<2) return -EPERM;
    if(copy_from_user(&io,arg,sizeof(io))) return -EFAULT;
    if(io.claim.reserved) return -EINVAL;
    memset(&io.report,0,sizeof(io.report));
    io.report.version=K2S_VERSION;
    io.report.execute_available=0;
    io.report.watchdog_step=K2S_WDT_DEFERRED;
    /* 1. Quiesce verdict. The module reads the core state itself (cpu_online,
     * a bit test that imports only cpu_online_mask); the driver, touch, USB and
     * rootfs facts are userspace's claim, checked the same way the dry-run
     * scripts did. The watchdog is not fed here, so seconds_since_feed is 0 to
     * let the predicate be reached; the report records the feed as deferred. */
    memset(&qs,0,sizeof(qs));
    io.report.online_cpus=(cpu_online(0)?1u:0u)+(cpu_online(1)?1u:0u);
    qs.online_cpus=io.report.online_cpus;
    qs.expected_cpus=1;
    qs.drivers_present=io.claim.drivers_present;
    qs.drivers_checked=KOA2_Q_DRIVER_COUNT;
    qs.rootfs_readonly=io.claim.rootfs_readonly?1u:0u;
    qs.touch_driver_present=io.claim.touch_driver_present?1u:0u;
    qs.touch_quiesced_verified=io.claim.touch_quiesced_verified?1u:0u;
    qs.usb_driver_present=io.claim.usb_driver_present?1u:0u;
    qs.usb_quiesced_verified=io.claim.usb_quiesced_verified?1u:0u;
    qs.seconds_since_feed=0;
    io.report.quiesce_result=(u32)koa2_quiesce_verify(&qs);
    /* 2. Re-audit the sealed copy list; the count must match the seal's. */
    io.report.copylist_result=(u32)exec_reaudit_list(s,&audited);
    io.report.copylist_entries=(u32)audited;
    if(!io.report.copylist_result && audited!=s->list_entries)
        io.report.copylist_result=(u32)KOA2_BOUNDS;
    /* 3. Arm the trampoline in a page the module owns and fill its parameter
     * block through the shared koa2_jump_fill, then give the page straight
     * back: a dry-run leaves nothing armed. */
#ifdef __KERNEL__
    blob=(size_t)(koa2_jump_end-koa2_jump_start);
#else
    blob=KOA2_EXEC_HOST_BLOB;
#endif
    io.report.trampoline_bytes=(u32)blob;
    io.report.expected_pages=exec_expected_pages(s);
    if(blob && blob<=PAGE_SIZE && allocate_owned(s)==0) {
        control_index=s->count-1;
        control=owned_phys(s,control_index);
        memcpy(owned_addr(s,control_index),koa2_jump_start,blob);
        in.list=owned_phys(s,s->list_first);
        in.entry=(u32)s->plan.segments[0].dest;
        in.fdt=(u32)s->plan.segments[1].dest;
        in.machine=0xffffffffu;   /* DT boot passes ~0 in r1; the exact value is a next-batch check */
        in.ram_start=0x80000000u;
        in.ram_end=KOA2_LOWMEM_END;
        in.budget=s->list_entries;
        in.control=control;
        in.expected=io.report.expected_pages;
        prc=koa2_jump_fill(&params,&in);
        if(!prc && !exec_control_clear(s,control)) prc=KOA2_RANGE;
        free_record(&s->owned[control_index]); s->count--;
    }
    io.report.params_result=(u32)prc;
    /* 4. Read the CPU state with IRQ and FIQ masked, as the step before the
     * caches would. These are mrc reads only -- no coprocessor write, nothing
     * that stops a device -- and the mask is lifted immediately. */
    local_irq_save(flags);
    local_fiq_disable();
#ifdef __KERNEL__
    __asm__ volatile("mrs %0, cpsr":"=r"(cpsr));
    __asm__ volatile("mrc p15,0,%0,c1,c0,0":"=r"(sctlr));
    __asm__ volatile("mrc p15,0,%0,c2,c0,2":"=r"(ttbcr));
#else
    cpsr=kshim_cpsr; sctlr=kshim_sctlr; ttbcr=kshim_ttbcr;
#endif
    local_fiq_enable();
    local_irq_restore(flags);
    io.report.sctlr=sctlr;
    io.report.ttbcr=ttbcr;
    io.report.mmu_on=(sctlr&1u)?1u:0u;
    io.report.caches_on=(sctlr&((1u<<2)|(1u<<12)))?1u:0u;
    io.report.irq_fiq_masked=((cpsr&0xc0u)==0xc0u)?1u:0u;
    /* 5. Stop before KOA2_Q_CACHES. Nothing was left armed; the stock system is
     * exactly as it was. */
    io.report.stopped_before_caches=1;
    if(copy_to_user(arg,&io,sizeof(io))) rc=-EFAULT;
    return rc;
}
