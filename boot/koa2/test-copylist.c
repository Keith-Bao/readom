/* SPDX-License-Identifier: GPL-2.0-only */
/* The copy list, checked by walking it the way the jump stage will.
 *
 * The walker below is the model of that code: it follows only the tags, has no
 * access to the segments, and writes into a flat array standing in for
 * physical memory. If the list is right, the array ends up holding each
 * payload at its destination; that is the property the device depends on. */
#include "copylist.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PAGE 0x1000u
#define RAM_BASE 0x80000000u
#define RAM_PAGES 3400u             /* stand-in for RAM: enough for a list of several pages */
static unsigned char *ram;          /* ram[(phys - RAM_BASE)] */
static unsigned char *at(uint32_t phys) { assert(phys >= RAM_BASE && phys - RAM_BASE < RAM_PAGES * PAGE); return ram + (phys - RAM_BASE); }
/* Walks the list from its first page, copying as it goes. Returns the number of
 * pages it copied, or -1 if it saw something it could not read. */
static long walk(uint32_t first)
{
    uint32_t dest = 0;
    long copied = 0;
    const uint32_t *page = (const uint32_t *)at(first);
    size_t word = 0;
    int have_dest = 0;
    for (;;) {
        uint32_t entry;
        if (word == KOA2_COPY_WORDS) return -1;
        entry = page[word++];
        switch (entry & KOA2_COPY_TAGS) {
        case KOA2_COPY_DEST: dest = entry & ~KOA2_COPY_TAGS; have_dest = 1; break;
        case KOA2_COPY_SOURCE:
            if (!have_dest) return -1;
            memcpy(at(dest), at(entry & ~KOA2_COPY_TAGS), PAGE);
            dest += PAGE; copied++;
            break;
        case KOA2_COPY_LINK:
            page = (const uint32_t *)at(entry & ~KOA2_COPY_TAGS); word = 0; break;
        case KOA2_COPY_DONE: return copied;
        default: return -1;
        }
    }
}
/* Pages of the stand-in RAM, handed out in order, each filled with a pattern. */
static uint32_t next_page = RAM_BASE;
static uint32_t take(unsigned char fill)
{
    uint32_t p = next_page;
    next_page += PAGE;
    memset(at(p), fill, PAGE);
    return p;
}
static uint32_t *virt(uint32_t phys) { return (uint32_t *)at(phys); }
static void one_page_list(void)
{
    uint32_t sources[8], list_physical[4], dest_a, dest_b;
    uint32_t *list_virtual[4];
    const uint32_t *read_virtual[4];
    struct koa2_copy_segment segments[2];
    struct koa2_range forbidden[1];
    size_t entries = 0, pages = 0, i;
    /* Two segments: 3 pages and 2 pages, into two destinations that are not
     * where the sources are. */
    for (i = 0; i < 5; i++) sources[i] = take((unsigned char)(0x11 * (i + 1)));
    dest_a = take(0); take(0); take(0);             /* 3 pages of destination */
    dest_b = take(0); take(0);                      /* 2 pages */
    list_physical[0] = take(0);
    list_virtual[0] = virt(list_physical[0]);
    read_virtual[0] = list_virtual[0];
    segments[0].dest = dest_a; segments[0].pages = sources; segments[0].page_count = 3;
    segments[1].dest = dest_b; segments[1].pages = sources + 3; segments[1].page_count = 2;
    assert(koa2_copylist_pages(segments, 2) == 1);
    assert(koa2_copylist_build(list_virtual, list_physical, 1, segments, 2, &entries) == KOA2_OK);
    assert(entries == 2 + 5 + 1);
    forbidden[0].start = RAM_BASE + (RAM_PAGES - 1) * PAGE; forbidden[0].size = PAGE;
    assert(koa2_copylist_audit(read_virtual, list_physical, 1, segments, 2, forbidden, 1, &entries, &pages) == KOA2_OK);
    assert(entries == 8 && pages == 1);
    /* Walking it puts every payload byte at its destination. */
    assert(walk(list_physical[0]) == 5);
    for (i = 0; i < 3; i++) assert(!memcmp(at(dest_a + i * PAGE), at(sources[i]), PAGE));
    for (i = 0; i < 2; i++) assert(!memcmp(at(dest_b + i * PAGE), at(sources[3 + i]), PAGE));
}
/* A payload big enough that the list does not fit in one page: the writer has
 * to chain, and the walker has to follow. */
static void linked_list(void)
{
    enum { N = 1100 };
    static uint32_t sources[N];
    uint32_t list_physical[2], dest, *list_virtual[2];
    const uint32_t *read_virtual[2];
    struct koa2_copy_segment segment;
    size_t entries = 0, pages = 0, i;
    for (i = 0; i < N; i++) sources[i] = take((unsigned char)(i * 7 + 1));
    dest = next_page;
    for (i = 0; i < N; i++) take(0);
    for (i = 0; i < 2; i++) {
        list_physical[i] = take(0);
        list_virtual[i] = virt(list_physical[i]);
        read_virtual[i] = list_virtual[i];
    }
    segment.dest = dest; segment.pages = sources; segment.page_count = N;
    assert(koa2_copylist_pages(&segment, 1) == 2);
    assert(koa2_copylist_build(list_virtual, list_physical, 2, &segment, 1, &entries) == KOA2_OK);
    assert(entries == 1 + N + 1 + 1);   /* dest, sources, the link, the terminator */
    assert((list_virtual[0][KOA2_COPY_WORDS - 1] & KOA2_COPY_TAGS) == KOA2_COPY_LINK);
    assert((list_virtual[0][KOA2_COPY_WORDS - 1] & ~KOA2_COPY_TAGS) == list_physical[1]);
    assert(koa2_copylist_audit(read_virtual, list_physical, 2, &segment, 1, NULL, 0, &entries, &pages) == KOA2_OK);
    assert(pages == 2);
    assert(walk(list_physical[0]) == N);
    for (i = 0; i < N; i++) assert(!memcmp(at(dest + i * PAGE), at(sources[i]), PAGE));
    /* One page short: the writer refuses rather than running off the end. */
    assert(koa2_copylist_build(list_virtual, list_physical, 1, &segment, 1, &entries) == KOA2_BOUNDS && !entries);
}
/* Everything the audit must refuse. Each starts from a list that passes. */
static void refusals(void)
{
    uint32_t sources[3], list_physical[2], dest, *list_virtual[2], saved;
    const uint32_t *read_virtual[2];
    struct koa2_copy_segment segments[2];
    struct koa2_range forbidden[1];
    size_t entries, pages, i;
    for (i = 0; i < 3; i++) sources[i] = take((unsigned char)(0x20 + i));
    dest = next_page; for (i = 0; i < 3; i++) take(0);
    for (i = 0; i < 2; i++) { list_physical[i] = take(0); list_virtual[i] = virt(list_physical[i]); read_virtual[i] = list_virtual[i]; }
    segments[0].dest = dest; segments[0].pages = sources; segments[0].page_count = 3;
    assert(koa2_copylist_build(list_virtual, list_physical, 1, segments, 1, &entries) == KOA2_OK);
    assert(koa2_copylist_audit(read_virtual, list_physical, 1, segments, 1, NULL, 0, &entries, &pages) == KOA2_OK);
    /* A changed entry: wrong destination, a lost tag, wrong source. */
    saved = list_virtual[0][0]; list_virtual[0][0] = saved + PAGE;
    assert(koa2_copylist_audit(read_virtual, list_physical, 1, segments, 1, NULL, 0, &entries, &pages) == KOA2_FORMAT);
    list_virtual[0][0] = saved & ~KOA2_COPY_TAGS;
    assert(koa2_copylist_audit(read_virtual, list_physical, 1, segments, 1, NULL, 0, &entries, &pages) == KOA2_FORMAT);
    list_virtual[0][0] = saved;
    saved = list_virtual[0][2]; list_virtual[0][2] ^= PAGE;
    assert(koa2_copylist_audit(read_virtual, list_physical, 1, segments, 1, NULL, 0, &entries, &pages) == KOA2_FORMAT);
    list_virtual[0][2] = saved;
    /* Anything after the terminator. */
    list_virtual[0][5] = sources[0] | KOA2_COPY_SOURCE;
    assert(koa2_copylist_audit(read_virtual, list_physical, 1, segments, 1, NULL, 0, &entries, &pages) == KOA2_FORMAT);
    list_virtual[0][5] = 0;
    /* A link to a page that is not the next one of this list. */
    list_virtual[0][KOA2_COPY_WORDS - 1] = list_physical[1] | KOA2_COPY_LINK;
    assert(koa2_copylist_audit(read_virtual, list_physical, 1, segments, 1, NULL, 0, &entries, &pages) == KOA2_FORMAT);
    list_virtual[0][KOA2_COPY_WORDS - 1] = 0;
    /* A list page the walker never reaches. */
    assert(koa2_copylist_audit(read_virtual, list_physical, 2, segments, 1, NULL, 0, &entries, &pages) == KOA2_FORMAT);
    assert(koa2_copylist_audit(read_virtual, list_physical, 1, segments, 1, NULL, 0, &entries, &pages) == KOA2_OK);
    /* A source inside a destination: the copy order would decide the result. */
    segments[1].dest = sources[0]; segments[1].pages = sources + 2; segments[1].page_count = 1;
    assert(koa2_copylist_build(list_virtual, list_physical, 1, segments, 2, &entries) == KOA2_OK);
    assert(koa2_copylist_audit(read_virtual, list_physical, 1, segments, 2, NULL, 0, &entries, &pages) == KOA2_RANGE);
    /* A list page inside a destination: the walker would overwrite its own list. */
    segments[1].dest = list_physical[0];
    assert(koa2_copylist_audit(read_virtual, list_physical, 1, segments, 2, NULL, 0, &entries, &pages) == KOA2_RANGE);
    /* A source that is a list page. */
    segments[0].pages = list_physical; segments[0].page_count = 1;
    assert(koa2_copylist_audit(read_virtual, list_physical, 1, segments, 1, NULL, 0, &entries, &pages) == KOA2_RANGE);
    segments[0].pages = sources; segments[0].page_count = 3;
    /* A forbidden range holding a source or a list page. */
    forbidden[0].start = sources[1]; forbidden[0].size = PAGE;
    assert(koa2_copylist_build(list_virtual, list_physical, 1, segments, 1, &entries) == KOA2_OK);
    assert(koa2_copylist_audit(read_virtual, list_physical, 1, segments, 1, forbidden, 1, &entries, &pages) == KOA2_RANGE);
    forbidden[0].start = list_physical[0];
    assert(koa2_copylist_audit(read_virtual, list_physical, 1, segments, 1, forbidden, 1, &entries, &pages) == KOA2_RANGE);
    /* The same page twice in one segment. */
    { uint32_t twice[2] = { sources[0], sources[0] };
      struct koa2_copy_segment s = { dest, twice, 2 };
      assert(koa2_copylist_build(list_virtual, list_physical, 1, &s, 1, &entries) == KOA2_OK);
      assert(koa2_copylist_audit(read_virtual, list_physical, 1, &s, 1, NULL, 0, &entries, &pages) == KOA2_RANGE); }
    /* Unaligned and zero addresses, empty segments, missing arguments. */
    { struct koa2_copy_segment s = { dest + 1, sources, 1 };
      assert(koa2_copylist_build(list_virtual, list_physical, 1, &s, 1, &entries) == KOA2_RANGE); }
    { uint32_t bad[1] = { sources[0] + 8 };
      struct koa2_copy_segment s = { dest, bad, 1 };
      assert(koa2_copylist_build(list_virtual, list_physical, 1, &s, 1, &entries) == KOA2_RANGE); }
    { uint32_t zero[1] = { 0 };
      struct koa2_copy_segment s = { dest, zero, 1 };
      assert(koa2_copylist_build(list_virtual, list_physical, 1, &s, 1, &entries) == KOA2_RANGE); }
    { struct koa2_copy_segment s = { dest, sources, 0 };
      assert(!koa2_copylist_pages(&s, 1));
      assert(koa2_copylist_build(list_virtual, list_physical, 1, &s, 1, &entries) == KOA2_BOUNDS); }
    assert(!koa2_copylist_pages(NULL, 1) && !koa2_copylist_pages(segments, 0));
    assert(koa2_copylist_build(NULL, list_physical, 1, segments, 1, &entries) == KOA2_BOUNDS);
    assert(koa2_copylist_build(list_virtual, list_physical, 0, segments, 1, &entries) == KOA2_BOUNDS);
    assert(koa2_copylist_audit(read_virtual, list_physical, 1, segments, 1, NULL, 1, &entries, &pages) == KOA2_BOUNDS);
    /* A list page that is not page-aligned, or listed twice. */
    { uint32_t bad[1] = { list_physical[0] + 4 };
      assert(koa2_copylist_build(list_virtual, bad, 1, segments, 1, &entries) == KOA2_RANGE); }
    { uint32_t twice[2] = { list_physical[0], list_physical[0] };
      uint32_t *v[2] = { list_virtual[0], list_virtual[0] };
      assert(koa2_copylist_build(v, twice, 2, segments, 1, &entries) == KOA2_RANGE); }
}
int main(void)
{
    ram = calloc(RAM_PAGES, PAGE);
    assert(ram);
    one_page_list();
    linked_list();
    refusals();
    puts("KOA2 copy list: build, audit and a walk of the written list agree; links, overlaps and damage refused");
    free(ram);
    return 0;
}
