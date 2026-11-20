/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef KOA2_CONTRACT_H
#define KOA2_CONTRACT_H
#include "koa2-types.h"
enum koa2_error { KOA2_OK=0, KOA2_BOUNDS=-1, KOA2_FORMAT=-2, KOA2_IDENTITY=-3, KOA2_RANGE=-4 };
struct koa2_fdt_info { uint32_t total, nodes, properties, reservations; };
/* Read-only: validates the complete envelope/token stream and Zelda identity.
 * Does not certify board calibration, memory layout, bootargs or safe boot. */
int koa2_fdt_validate(const void *data, size_t bytes, struct koa2_fdt_info *info);
struct koa2_range { uint64_t start, size; };
int koa2_range_valid(struct koa2_range value);
int koa2_range_overlap(struct koa2_range a, struct koa2_range b);
/* A payload must fit wholly within one allowed RAM interval and avoid all
 * exclusions. This never selects addresses or copies/executes payloads. */
int koa2_range_allowed(struct koa2_range payload,
                     const struct koa2_range *ram, size_t ram_count,
                     const struct koa2_range *excluded, size_t excluded_count);
/* Physical layout measured on one Oasis 2, firmware 5.16.2.1.1 (Boot Preflight v1,
 * see ports/koa2/boot-preflight-findings.md). Facts about that device, not defaults. */
#define KOA2_MEASURED_RAM_COUNT 2
#define KOA2_MEASURED_EXCLUDED_COUNT 7
extern const struct koa2_range koa2_measured_ram[KOA2_MEASURED_RAM_COUNT];
extern const struct koa2_range koa2_measured_excluded[KOA2_MEASURED_EXCLUDED_COUNT];
/* Copies the FDT header reserve map of an FDT that koa2_fdt_validate accepts.
 * Fails if it holds more than max entries; *count is 0 on any failure. */
int koa2_fdt_reservations(const void *data, size_t bytes,
                          struct koa2_range *out, size_t max, size_t *count);
/* KOA2_OK only if every reservation lies wholly inside one exclusion, so a
 * bootloader that reserves something new is refused instead of overwritten. */
int koa2_reservations_covered(const struct koa2_range *reservations, size_t n,
                              const struct koa2_range *excluded, size_t m);
#endif
