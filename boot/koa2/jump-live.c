/* SPDX-License-Identifier: GPL-2.0-only */
/* Stage F live jump path, #included at the tail of stage.c ONLY when
 * KOA2_JUMP_LIVE is defined (koa2_handoff_jump.ko). */
#ifdef KOA2_JUMP_LIVE
#ifdef __KERNEL__
#include <linux/io.h>
extern unsigned char koa2_jump_params[];
#endif
#define K2S_WDOG1_PHYS 0x30280000u
#define K2S_WDOG1_SIZE 0x10u
#define K2S_WDOG_WSR   0x02u

static int exec_jump(struct store *s, void __user *arg)
{
    struct k2s_exec_jump io;
    struct koa2_quiesce_state qs;
    struct koa2_jump_inputs in;
    struct kj_params params;
    size_t blob, params_off, audited=0;
    u32 ttbcr=0, control=0;
    unsigned int control_index;
    unsigned char *control_va;
#ifdef __KERNEL__
    void __iomem *wdog;
#endif
    if(s->state!=K2S_SEALED || !s->plan_valid) return -EPERM;
    if(s->plan.segment_count<2) return -EPERM;
    if(copy_from_user(&io,arg,sizeof(io))) return -EFAULT;
    if(io.magic!=K2S_JUMP_MAGIC || io.claim.reserved) return -EINVAL;
    memset(&qs,0,sizeof(qs));
    qs.online_cpus=(cpu_online(0)?1u:0u)+(cpu_online(1)?1u:0u);
    qs.expected_cpus=1;
    qs.drivers_present=io.claim.drivers_present;
    qs.drivers_checked=KOA2_Q_DRIVER_COUNT;
    qs.rootfs_readonly=io.claim.rootfs_readonly?1u:0u;
    qs.touch_driver_present=io.claim.touch_driver_present?1u:0u;
    qs.touch_quiesced_verified=io.claim.touch_quiesced_verified?1u:0u;
    qs.usb_driver_present=io.claim.usb_driver_present?1u:0u;
    qs.usb_quiesced_verified=io.claim.usb_quiesced_verified?1u:0u;
    qs.seconds_since_feed=0;
    if(koa2_quiesce_verify(&qs)!=0) return -EBUSY;
    if(exec_reaudit_list(s,&audited)!=0 || audited!=s->list_entries) return -EIO;
#ifdef __KERNEL__
    blob=(size_t)(koa2_jump_end-koa2_jump_start);
    params_off=(size_t)(koa2_jump_params-koa2_jump_start);
    __asm__ volatile("mrc p15,0,%0,c2,c0,2":"=r"(ttbcr));
#else
    blob=KOA2_EXEC_HOST_BLOB;
    params_off=0;
    ttbcr=kshim_ttbcr;
#endif
    if(!blob || blob>PAGE_SIZE || params_off+sizeof(params)>blob || ttbcr!=0) return -EINVAL;
    if(allocate_owned(s)!=0) return -ENOMEM;
    control_index=s->count-1;
    control=owned_phys(s,control_index);
    control_va=(unsigned char *)owned_addr(s,control_index);
    memcpy(control_va,koa2_jump_start,blob);
    in.list=owned_phys(s,s->list_first);
    in.entry=(u32)s->plan.segments[0].dest;
    in.fdt=(u32)s->plan.segments[1].dest;
    in.machine=0xffffffffu;
    in.ram_start=0x80000000u;
    in.ram_end=KOA2_LOWMEM_END;
    in.budget=s->list_entries;
    in.control=control;
    in.expected=exec_expected_pages(s);
    if(koa2_jump_fill(&params,&in)!=0 || !exec_control_clear(s,control)) {
        free_record(&s->owned[control_index]); s->count--;
        return -EINVAL;
    }
    memcpy(control_va+params_off,&params,sizeof(params));
#ifdef __KERNEL__
    wdog=ioremap(K2S_WDOG1_PHYS,K2S_WDOG1_SIZE);
    if(!wdog) {
        free_record(&s->owned[control_index]); s->count--;
        return -ENOMEM;
    }
    local_irq_disable();
    local_fiq_disable();
    writew(0x5555u,wdog+K2S_WDOG_WSR);
    writew(0xAAAAu,wdog+K2S_WDOG_WSR);
    flush_cache_all();
    __asm__ volatile(
        "dsb\n\t"
        "isb\n\t"
        "mrc p15, 0, r0, c1, c0, 0\n\t"
        "bic r0, r0, #(1 << 2)\n\t"
        "bic r0, r0, #(1 << 12)\n\t"
        "mcr p15, 0, r0, c1, c0, 0\n\t"
        "isb\n\t"
        "mov r0, #0\n\t"
        "mcr p15, 0, r0, c7, c5, 0\n\t"
        "mcr p15, 0, r0, c7, c5, 6\n\t"
        "dsb\n\t"
        "isb\n\t"
        "bx %0\n\t"
        :
        : "r"(control)
        : "r0", "memory"
    );
    for(;;) ;
#else
    free_record(&s->owned[control_index]); s->count--;
    return 0;
#endif
}
#endif
