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
#endif
