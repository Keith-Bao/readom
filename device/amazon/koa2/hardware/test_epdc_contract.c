/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdio.h>
#include "EpdcContract.h"

int main(void) {
    assert(KOA2_EPDC_WIDTH == 1264u);
    assert(KOA2_EPDC_HEIGHT == 1680u);
    assert(KOA2_EPDC_STRIDE == 1280u);
    assert(KOA2_EPDC_FB_BYTES == 1280u * 1680u);
    assert(koa2_epdc_pixel_offset(10, 2) == 2570u);

    /* Safe waveforms pass; night mode and inverted grayscale are hard-rejected. */
    assert(koa2_epdc_waveform_allowed(KOA2_WF_GC16, false));
    assert(koa2_epdc_waveform_allowed(KOA2_WF_DU, false));
    assert(koa2_epdc_waveform_allowed(KOA2_WF_GLR16, false));
    assert(koa2_epdc_waveform_allowed(KOA2_WF_A2, false));
    assert(!koa2_epdc_waveform_allowed(KOA2_WF_GCK16, false));
    assert(!koa2_epdc_waveform_allowed(KOA2_WF_GLKW16, false));
    assert(!koa2_epdc_waveform_allowed(KOA2_WF_GC16, true));
    assert(!koa2_epdc_waveform_allowed(0x99u, false));

    struct koa2_epdc_rect r;
    assert(koa2_epdc_clamp_rect(10, 20, 100, 200, &r));
    assert(r.left == 10 && r.top == 20 && r.width == 100 && r.height == 200);
    assert(koa2_epdc_clamp_rect(-10, -5, 30, 20, &r));
    assert(r.left == 0 && r.top == 0 && r.width == 20 && r.height == 15);
    assert(koa2_epdc_clamp_rect(1200, 1600, 200, 200, &r));
    assert(r.left == 1200 && r.top == 1600 && r.width == 64 && r.height == 80);
    assert(!koa2_epdc_clamp_rect(1264, 0, 10, 10, &r));
    assert(!koa2_epdc_clamp_rect(0, 0, 0, 10, &r));
    assert(!koa2_epdc_clamp_rect(-50, 0, 20, 20, &r));

    printf("KOA2 EPDC contract: 1264x1680 stride-1280 geometry, rect clamping, night-mode hard reject\n");
    return 0;
}
