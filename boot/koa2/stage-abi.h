/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef KOA2_STAGE_ABI_H
#define KOA2_STAGE_ABI_H
#ifdef __KERNEL__
#include <linux/ioctl.h>
#else
#include <sys/ioctl.h>
#endif
/* /dev/koa2-handoff-stage: stages the three payloads in pages the module owns,
 * checks them, rewrites the FDT and validates the plan. It has no jump: there
 * is no ioctl that copies to a destination, stops a device or leaves Linux. */
typedef unsigned int k2s_u32;
typedef unsigned long long k2s_u64;
typedef unsigned char k2s_u8;
#define K2S_VERSION 1u
#define K2S_SLOTS 3u     /* 0 zImage, 1 the stock FDT as read from /sys/firmware/fdt, 2 initramfs */
#define K2S_EMPTY 0u
#define K2S_WRITING 1u
#define K2S_SEALED 2u
/* Callers name sizes and hashes; destinations are chosen by the module. */
struct k2s_image { k2s_u32 bytes; k2s_u8 sha256[32]; };
struct k2s_begin { k2s_u32 version, reserved; struct k2s_image images[K2S_SLOTS]; };
struct k2s_put { k2s_u32 slot, offset, bytes, reserved; k2s_u64 user_buffer; };
/* Data only; no physical address leaves the kernel. */
struct k2s_status {
    k2s_u32 version, state, written[K2S_SLOTS], sizes[K2S_SLOTS];
    k2s_u32 payload_pages, collision_pages, fdt_bytes;
    k2s_u32 list_pages, list_entries;
    signed int seal_stage, seal_result;  /* which check of seal() failed last, and its code */
    k2s_u32 plan_valid, execute_available;
    k2s_u32 marked;      /* markers placed since the seal; execute_available stays 0 */
};
#define K2S_BEGIN _IOW('K',0x31,struct k2s_begin)
#define K2S_PUT _IOW('K',0x32,struct k2s_put)
#define K2S_SEAL _IO('K',0x33)
#define K2S_STATUS _IOR('K',0x34,struct k2s_status)
#define K2S_DISCARD _IO('K',0x35)
/* seal_stage values */
#define K2S_STAGE_INCOMPLETE 1
#define K2S_STAGE_HASH 2
#define K2S_STAGE_PINNED 3
#define K2S_STAGE_FDT 4
#define K2S_STAGE_FDT_RESERVATIONS 5
#define K2S_STAGE_REWRITE 6
#define K2S_STAGE_MEMORY 7
#define K2S_STAGE_PLAN 8
#define K2S_STAGE_PAGES 9
#define K2S_STAGE_COPYLIST 10
/* K2S_MARK writes one marker line into the live half of the Lab126 printk
 * buffer and cleans it out to DRAM, so that a jump which dies before the new
 * kernel's printk can be told apart from a jump that never happened at all
 * (ports/koa2/recovery.md section 4).
 *
 * It is not a jump and does not enable one: nothing is copied to a destination,
 * no device is stopped, no core goes down, and execute_available stays 0. What
 * it costs is the last 256 bytes of this boot's kernel log.
 *
 * It needs a sealed plan, because the entry and FDT addresses it records are
 * that plan's. attempt is the caller's own counter, so that repeated attempts
 * can be told apart in the buffer afterwards. */
struct k2s_mark { k2s_u32 attempt, reserved; };
#define K2S_MARK _IOW('K',0x36,struct k2s_mark)
/* The execute module (koa2_handoff_exec.ko, built with KOA2_EXEC_BUILD) adds a
 * dry-run that the staging module does not have: it quiesce-verifies, re-audits
 * the sealed copy list, arms the trampoline in a page it owns and fills its
 * parameter block, then masks IRQ/FIQ to read the CPU state read-only -- and
 * stops there, one step short of the point of no return (disabling the caches),
 * putting everything back. It never copies a payload, stops no device itself,
 * takes no core down and does not jump. The staging build does not define the
 * ioctl and returns -ENOTTY. execute_available stays 0 in both. */
struct k2s_exec_claim {
    /* What userspace established before calling, by the same reversible paths
     * the dry-run scripts use (ports/koa2/handoff-quiesce.md). The module does
     * not trust these for the CPU count, which it reads itself. */
    k2s_u32 drivers_present;            /* of the four quiesce drivers, how many are still loaded */
    k2s_u32 touch_driver_present;       /* cyttsp5_i2c still loaded but verified asleep */
    k2s_u32 touch_quiesced_verified;
    k2s_u32 usb_driver_present;         /* g_mass_storage still loaded but verified unplugged/idle */
    k2s_u32 usb_quiesced_verified;
    k2s_u32 rootfs_readonly;            /* 1 when userspace remounted / read-only */
    k2s_u32 reserved;
};
/* Watchdog handling in the dry-run: it is not fed, because feeding it belongs
 * at the point of no return, which the dry-run stops before. */
#define K2S_WDT_DEFERRED 0u
struct k2s_exec_report {
    k2s_u32 version;
    k2s_u32 online_cpus;          /* read by the module: cpu_online(0)+cpu_online(1) */
    k2s_u32 quiesce_result;       /* koa2_quiesce_verify() return (0 = quiesced) */
    k2s_u32 copylist_result;      /* re-audit of the sealed list (0 = good) */
    k2s_u32 copylist_entries;     /* entries re-audited; must equal the sealed list_entries */
    k2s_u32 expected_pages;       /* payload pages the trampoline is told to copy */
    k2s_u32 params_result;        /* koa2_jump_fill() return (0 = armed) */
    k2s_u32 trampoline_bytes;     /* koa2_jump_end - koa2_jump_start; must be <= one page */
    k2s_u32 sctlr;                /* CP15 SCTLR, read-only under IRQ/FIQ masked */
    k2s_u32 ttbcr;                /* CP15 TTBCR; the trampoline needs it 0 */
    k2s_u32 mmu_on;               /* SCTLR.M */
    k2s_u32 caches_on;            /* SCTLR.C or SCTLR.I */
    k2s_u32 irq_fiq_masked;       /* both masked during the read-only snapshot */
    k2s_u32 watchdog_step;        /* K2S_WDT_DEFERRED: not fed in a dry-run */
    k2s_u32 execute_available;    /* always 0 */
    k2s_u32 stopped_before_caches;/* 1 when it reached the step before caches and put everything back */
};
struct k2s_exec_dryrun { struct k2s_exec_claim claim; struct k2s_exec_report report; };
#define K2S_EXEC_DRYRUN _IOWR('K',0x37,struct k2s_exec_dryrun)
#endif
