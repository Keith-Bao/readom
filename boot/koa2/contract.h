/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef KOA2_CONTRACT_H
#define KOA2_CONTRACT_H
#include <stddef.h>
#include <stdint.h>
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
#endif
