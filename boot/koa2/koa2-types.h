/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef KOA2_TYPES_H
#define KOA2_TYPES_H
/* The checkers build twice: on the host for the tests, and inside the
 * handoff module, where there is no libc. */
#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#ifndef UINT64_MAX
#define UINT64_MAX U64_MAX
#endif
#ifndef UINT32_MAX
#define UINT32_MAX U32_MAX
#endif
#else
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#endif
#endif
