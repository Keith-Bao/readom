/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Kindle Oasis 2 (KOA2 / i.MX7D) EPDC v2 display geometry and safe-update contract.
 *
 * Verified against hardware-findings.md, stock runtime DTB (epdc@306f0000,
 * reserved-memory/epdc_reserved 0x9c000000+5MiB), and ports/koa2/epdc-v1/:
 *  - Visible panel: 1264 x 1680, 8 bpp grayscale (ED070KC1 / ED070KC2UA).
 *  - Virtual stride: 1280 bytes/row (16-pixel alignment).
 *  - Night-mode waveforms (GCK16=0x08, GLKW16=0x09) are hard-rejected until
 *    the Lab126 0x04 VDC waveform table is decoded (ports/koa2/epdc-source-gap.md).
 */
#ifndef KOA2_HARDWARE_EPDC_CONTRACT_H
#define KOA2_HARDWARE_EPDC_CONTRACT_H

#include <stdbool.h>
#include <stdint.h>

#define KOA2_EPDC_WIDTH         1264u
#define KOA2_EPDC_HEIGHT        1680u
#define KOA2_EPDC_STRIDE        1280u
#define KOA2_EPDC_BPP           8u
#define KOA2_EPDC_FB_BYTES      (KOA2_EPDC_STRIDE * KOA2_EPDC_HEIGHT)

#define KOA2_WF_AUTO            0x101u
#define KOA2_WF_INIT            0x00u
#define KOA2_WF_DU              0x01u
#define KOA2_WF_GC16            0x02u
#define KOA2_WF_GL16            0x03u
#define KOA2_WF_GLR16           0x04u
#define KOA2_WF_A2              0x06u
#define KOA2_WF_GCK16           0x08u
#define KOA2_WF_GLKW16          0x09u

struct koa2_epdc_rect {
    uint32_t left;
    uint32_t top;
    uint32_t width;
    uint32_t height;
};

static inline bool koa2_epdc_waveform_allowed(uint32_t waveform_mode, bool inverted_grayscale) {
    if (inverted_grayscale) return false;
    if (waveform_mode == KOA2_WF_GCK16 || waveform_mode == KOA2_WF_GLKW16) return false;
    switch (waveform_mode) {
        case KOA2_WF_AUTO:
        case KOA2_WF_INIT:
        case KOA2_WF_DU:
        case KOA2_WF_GC16:
        case KOA2_WF_GL16:
        case KOA2_WF_GLR16:
        case KOA2_WF_A2:
            return true;
        default:
            return false;
    }
}

/* Clamp a dirty rectangle to the visible 1264x1680 panel; returns false if empty. */
static inline bool koa2_epdc_clamp_rect(int32_t x, int32_t y, int32_t w, int32_t h,
                                        struct koa2_epdc_rect *out) {
    if (!out || w <= 0 || h <= 0) return false;
    int64_t x0 = x < 0 ? 0 : x;
    int64_t y0 = y < 0 ? 0 : y;
    int64_t x1 = (int64_t)x + (int64_t)w;
    int64_t y1 = (int64_t)y + (int64_t)h;
    if (x1 > (int64_t)KOA2_EPDC_WIDTH) x1 = KOA2_EPDC_WIDTH;
    if (y1 > (int64_t)KOA2_EPDC_HEIGHT) y1 = KOA2_EPDC_HEIGHT;
    if (x1 <= x0 || y1 <= y0) return false;
    out->left = (uint32_t)x0;
    out->top = (uint32_t)y0;
    out->width = (uint32_t)(x1 - x0);
    out->height = (uint32_t)(y1 - y0);
    return true;
}

static inline uint32_t koa2_epdc_pixel_offset(uint32_t x, uint32_t y) {
    return y * KOA2_EPDC_STRIDE + x;
}

#endif
