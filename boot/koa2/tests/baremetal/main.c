/* SPDX-License-Identifier: GPL-2.0-only */
/* Bare-metal exercise of boot/koa2/jump.S on an ARMv7 core: builds a copy list
 * with the same code the module uses, copies the trampoline into a page,
 * enters it with the MMU off, and lets it copy a stand-in kernel to its
 * destination and jump there. The stand-in prints KO! only if it was entered
 * with r0=0, r1=machine and r2 pointing at the FDT word it was given.
 *
 * This is the part the host tests cannot reach: the MMU and cache writes, the
 * PC-relative parameter block, and the register contract at the jump. */
#include "copylist.h"
#include "jump-abi.h"
#include "jump-params.h"
#define PAGE 0x1000u
#define RAM_BASE 0x40000000u
#define RAM_END 0x48000000u          /* QEMU is started with 128 MiB */
#define UART 0x09000000u
extern unsigned char fake_kernel_start[], fake_kernel_end[];
extern unsigned char _end[];
/* copylist.c needs these two from contract.c, which brings in string
 * functions this freestanding build has none of; they are three lines each. */
int koa2_range_valid(struct koa2_range v) { return v.size && v.start <= (uint64_t)-1 - v.size; }
int koa2_range_overlap(struct koa2_range a, struct koa2_range b)
{
    if (!koa2_range_valid(a) || !koa2_range_valid(b)) return 1;
    return a.start < b.start + b.size && b.start < a.start + a.size;
}
static void put(char c) { *(volatile unsigned int *)UART = (unsigned int)c; }
static void say(const char *s) { while (*s) put(*s++); }
static void hex(unsigned int v)
{
    int i;
    say("0x");
    for (i = 28; i >= 0; i -= 4) put("0123456789abcdef"[(v >> i) & 0xf]);
}
static void fail(const char *why) { say("FAIL "); say(why); put('\n'); for (;;) ; }
/* Pages above the test's own image, handed out in order. */
static unsigned int next_page;
static unsigned int take(void)
{
    unsigned int p = next_page;
    next_page += PAGE;
    if (p + PAGE > RAM_END) fail("out of pages");
    return p;
}
static void *as_pointer(unsigned int phys) { return (void *)phys; }
static void zero(unsigned int phys)
{
    unsigned char *p = as_pointer(phys);
    unsigned int i;
    for (i = 0; i < PAGE; i++) p[i] = 0;
}
static void copy(unsigned char *to, const unsigned char *from, unsigned int n)
{
    while (n--) *to++ = *from++;
}
/* ARMv7 cache-off sequence, as cpu_v7_reset's callers do it: clean and
 * invalidate every level by set/way, then clear C and I in SCTLR. The
 * caches_on case deliberately does not call it. */
__attribute__((unused)) static void caches_off(void)
{
    unsigned int clidr, level, sctlr;
    __asm__ volatile("dmb");
    __asm__ volatile("mrc p15, 1, %0, c0, c0, 1" : "=r"(clidr));
    for (level = 0; level < 7; level++) {
        unsigned int type = (clidr >> (level * 3)) & 7, csidr, line, ways, sets, way, set;
        if (type < 2) continue;
        __asm__ volatile("mcr p15, 2, %0, c0, c0, 0" :: "r"(level << 1));
        __asm__ volatile("isb");
        __asm__ volatile("mrc p15, 1, %0, c0, c0, 0" : "=r"(csidr));
        line = (csidr & 7) + 4;
        ways = ((csidr >> 3) & 0x3ff);
        sets = ((csidr >> 13) & 0x7fff);
        for (set = 0; set <= sets; set++)
            for (way = 0; way <= ways; way++) {
                unsigned int shift = 32, w = ways;
                while (w) { shift--; w >>= 1; }
                __asm__ volatile("mcr p15, 0, %0, c7, c14, 2"
                                 :: "r"((level << 1) | (way << shift) | (set << line)));
            }
    }
    __asm__ volatile("dsb");
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    sctlr &= ~((1u << 2) | (1u << 12));          /* C, I */
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" :: "r"(sctlr));
    __asm__ volatile("isb");
    __asm__ volatile("mcr p15, 0, %0, c7, c5, 0" :: "r"(0));   /* invalidate i-cache */
    __asm__ volatile("dsb");
    __asm__ volatile("isb");
}
/* Each case is one build. The default is the whole handoff; the others damage
 * one thing the trampoline has to refuse. */
#if !defined(CASE_pass) && !defined(CASE_caches_on) && !defined(CASE_bad_link) && \
    !defined(CASE_bad_source) && !defined(CASE_short_count) && !defined(CASE_budget)
#error "build with -DCASE_pass or another case"
#endif
void baremetal_main(void)
{
    unsigned int kernel_bytes = (unsigned int)(fake_kernel_end - fake_kernel_start);
    unsigned int kernel_pages = (kernel_bytes + PAGE - 1) / PAGE;
    unsigned int jump_bytes = (unsigned int)(koa2_jump_end - koa2_jump_start);
    unsigned int sources[4], fdt_source, dest, fdt_dest, control, list_phys[1];
    unsigned int *list_virt[1];
    const unsigned int *list_read[1];
    struct koa2_copy_segment segments[2];
    struct kj_params *params;
    unsigned int i, entries = 0, audited = 0;
    void (*enter)(void);
    say("koa2 bare-metal jump test\n");
    if (jump_bytes > PAGE) fail("trampoline over a page");
    if (kernel_pages > 4) fail("stand-in kernel too big");
    next_page = ((unsigned int)_end + PAGE - 1) & ~(PAGE - 1);
    /* The stand-in kernel, page by page, as if it had been staged. */
    for (i = 0; i < kernel_pages; i++) {
        unsigned int left = kernel_bytes - i * PAGE;
        sources[i] = take();
        zero(sources[i]);
        copy(as_pointer(sources[i]), fake_kernel_start + i * PAGE, left < PAGE ? left : PAGE);
    }
    /* A one-page stand-in for the FDT: the magic word the kernel checks. */
    fdt_source = take();
    zero(fdt_source);
    *(unsigned int *)as_pointer(fdt_source) = 0xd00dfeedu;
    dest = take();
    for (i = 1; i < kernel_pages; i++) take();
    fdt_dest = take();
    control = take();
    list_phys[0] = take();
    zero(dest); zero(fdt_dest); zero(control); zero(list_phys[0]);
    list_virt[0] = as_pointer(list_phys[0]);
    list_read[0] = list_virt[0];
    segments[0].dest = dest; segments[0].pages = sources; segments[0].page_count = kernel_pages;
    segments[1].dest = fdt_dest; segments[1].pages = &fdt_source; segments[1].page_count = 1;
    if (koa2_copylist_pages(segments, 2) != 1) fail("list should fit a page");
    if (koa2_copylist_build(list_virt, list_phys, 1, segments, 2, &entries)) fail("build");
    if (koa2_copylist_audit(list_read, list_phys, 1, segments, 2, 0, 0, &audited, 0)) fail("audit");
    if (audited != entries) fail("entries disagree");
    copy(as_pointer(control), koa2_jump_start, jump_bytes);
    params = (struct kj_params *)(control + (unsigned int)(koa2_jump_params - koa2_jump_start));
    {
        /* The same filler the module uses, so the two cannot drift. budget is
         * the audited entry count, as the module sets it. */
        struct koa2_jump_inputs in;
        in.list = list_phys[0];
        in.entry = dest;
        in.machine = 0xffffffffu;
        in.fdt = fdt_dest;
        in.ram_start = RAM_BASE;
        in.ram_end = RAM_END;
        in.budget = entries;
        in.control = control;
        in.expected = kernel_pages + 1;
        if (koa2_jump_fill(params, &in)) fail("jump params rejected");
    }
#ifdef CASE_bad_link
    /* A link out of RAM, where the first entry is. */
    list_virt[0][0] = 0x90000000u | 2u;
#endif
#ifdef CASE_bad_source
    /* The first source page, moved out of RAM. */
    list_virt[0][1] = 0x90000000u | 8u;
#endif
#ifdef CASE_short_count
    params->expected = kernel_pages + 2;
#endif
#ifdef CASE_budget
    params->budget = 2;
#endif
    say("entries "); hex(entries); say(" control "); hex(control); say(" dest "); hex(dest); put('\n');
#ifdef CASE_caches_on
    /* QEMU resets with the caches off, so this case has to turn them on to
     * give the trampoline something to refuse. */
    {   unsigned int sctlr;
        __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
        sctlr |= (1u << 2) | (1u << 12);
        __asm__ volatile("mcr p15, 0, %0, c1, c0, 0" :: "r"(sctlr));
        __asm__ volatile("isb"); }
#else
    caches_off();
#endif
    /* From here the verdict is read from outside: on success the stand-in
     * kernel prints KO!, and on a refusal the trampoline halts, so run.py
     * reads the status word and the destination through the QEMU monitor. */
    say("params "); hex((unsigned int)params); put(10);
    enter = (void (*)(void))control;
    enter();
    fail("trampoline returned");
}
