/* SPDX-License-Identifier: GPL-2.0-only */
#include "jump-params.h"
#include "contract.h"   /* koa2_error codes */
#define KJ_PAGE 0x1000u
static int page_in_ram(uint32_t base, uint32_t ram_start, uint32_t ram_end)
{
    if (base & (KJ_PAGE - 1)) return 0;            /* page-aligned */
    if (base < ram_start) return 0;
    if (base > ram_end - KJ_PAGE) return 0;        /* a whole page fits below ram_end */
    return 1;
}
int koa2_jump_fill(struct kj_params *out, const struct koa2_jump_inputs *in)
{
    if (!out || !in) return KOA2_BOUNDS;
    /* The one RAM window the trampoline bounds every address against. */
    if (in->ram_start & (KJ_PAGE - 1)) return KOA2_RANGE;
    if (in->ram_end <= in->ram_start) return KOA2_RANGE;
    if (in->ram_end & (KJ_PAGE - 1)) return KOA2_RANGE;
    /* entry, fdt, list and control are all whole pages inside it. The FDT
     * destination is MiB-aligned in the plan, so page alignment is no new
     * constraint; checking it here keeps the trampoline's own rule in one
     * place. */
    if (!page_in_ram(in->entry, in->ram_start, in->ram_end)) return KOA2_RANGE;
    if (!page_in_ram(in->fdt, in->ram_start, in->ram_end)) return KOA2_RANGE;
    if (!page_in_ram(in->list, in->ram_start, in->ram_end)) return KOA2_RANGE;
    if (!page_in_ram(in->control, in->ram_start, in->ram_end)) return KOA2_RANGE;
    /* The control page is where the blob runs from; jump.S refuses it as a
     * source, destination or list page, so it must not be the list head or the
     * entry here either. */
    if (in->control == in->list || in->control == in->entry ||
        in->control == in->fdt) return KOA2_RANGE;
    if (!in->budget || !in->expected) return KOA2_BOUNDS;
    if (in->budget < in->expected) return KOA2_BOUNDS;
    out->list = in->list;
    out->entry = in->entry;
    out->machine = in->machine;
    out->fdt = in->fdt;
    out->ram_start = in->ram_start;
    out->ram_end = in->ram_end;
    out->budget = in->budget;
    out->control = in->control;
    out->expected = in->expected;
    out->copied = 0;
    out->status = 0;
    return KOA2_OK;
}
