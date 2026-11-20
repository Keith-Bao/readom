/* SPDX-License-Identifier: GPL-2.0-only */
#include "copylist.h"
#define PAGE 0x1000u
/* Entries per page, less the word reserved for a link on every page but the last. */
#define USABLE (KOA2_COPY_WORDS - 1u)
static int page_aligned(uint64_t v) { return !(v & (PAGE - 1)); }
static int fits32(uint64_t v) { return v <= UINT32_MAX - PAGE + 1; }
static size_t needed(const struct koa2_copy_segment *s, size_t count, size_t *entries)
{
    size_t total = 1;   /* the terminator */
    size_t i;
    for (i = 0; i < count; i++) {
        if (!s[i].page_count || !s[i].pages) return 0;
        if (s[i].page_count > UINT32_MAX / PAGE) return 0;
        if (total > (size_t)-1 - 1 - s[i].page_count) return 0;
        total += 1 + s[i].page_count;
    }
    if (entries) *entries = total;
    return (total + USABLE - 1) / USABLE;
}
size_t koa2_copylist_pages(const struct koa2_copy_segment *segments, size_t count)
{
    if (!segments || !count) return 0;
    return needed(segments, count, NULL);
}
/* Writes value, moving to the next list page through a link when the current
 * page has only its link word left. */
struct writer { uint32_t *const *virtual; const uint32_t *physical; size_t count, page, word, written; };
static int emit(struct writer *w, uint32_t value)
{
    if (w->word == USABLE) {
        if (w->page + 1 >= w->count) return KOA2_BOUNDS;
        w->virtual[w->page][w->word] = w->physical[w->page + 1] | KOA2_COPY_LINK;
        w->page++; w->word = 0; w->written++;
    }
    w->virtual[w->page][w->word++] = value;
    w->written++;
    return KOA2_OK;
}
static int list_ok(const uint32_t *physical, size_t count)
{
    size_t i, j;
    for (i = 0; i < count; i++) {
        if (!physical[i] || !page_aligned(physical[i])) return 0;
        for (j = 0; j < i; j++) if (physical[i] == physical[j]) return 0;
    }
    return 1;
}
int koa2_copylist_build(uint32_t *const *list_virtual, const uint32_t *list_physical, size_t list_count,
                        const struct koa2_copy_segment *segments, size_t segment_count, size_t *entries)
{
    struct writer w;
    size_t i, j, want, count = 0;
    if (entries) *entries = 0;
    if (!list_virtual || !list_physical || !list_count || !segments || !segment_count) return KOA2_BOUNDS;
    if (!list_ok(list_physical, list_count)) return KOA2_RANGE;
    want = needed(segments, segment_count, &count);
    if (!want || want > list_count) return KOA2_BOUNDS;
    for (i = 0; i < segment_count; i++) {
        if (!page_aligned(segments[i].dest) || !fits32(segments[i].dest)) return KOA2_RANGE;
        for (j = 0; j < segments[i].page_count; j++)
            if (!segments[i].pages[j] || !page_aligned(segments[i].pages[j])) return KOA2_RANGE;
    }
    for (i = 0; i < list_count; i++) {
        size_t k;
        if (!list_virtual[i]) return KOA2_BOUNDS;
        for (k = 0; k < KOA2_COPY_WORDS; k++) list_virtual[i][k] = 0;
    }
    w.virtual = list_virtual; w.physical = list_physical; w.count = list_count;
    w.page = w.word = w.written = 0;
    for (i = 0; i < segment_count; i++) {
        int rc = emit(&w, (uint32_t)segments[i].dest | KOA2_COPY_DEST);
        if (rc) return rc;
        for (j = 0; j < segments[i].page_count; j++) {
            rc = emit(&w, segments[i].pages[j] | KOA2_COPY_SOURCE);
            if (rc) return rc;
        }
    }
    { int rc = emit(&w, KOA2_COPY_DONE); if (rc) return rc; }
    /* Every page but the last also spent a word on a link, so the words
     * written are the entries plus one per link. */
    if (w.page + 1 != want || w.written != count + want - 1) return KOA2_BOUNDS;
    if (entries) *entries = w.written;
    return KOA2_OK;
}
/* The audit is a second, independent reader on purpose: it walks the written
 * words and checks them against the segments, so a mistake in the writer above
 * is caught here rather than by the device. */
struct reader { const uint32_t *const *virtual; const uint32_t *physical; size_t count, page, word, read; };
static int next_word(struct reader *r, uint32_t *out)
{
    if (r->word == USABLE) {
        uint32_t link = r->virtual[r->page][r->word];
        size_t i, found = r->count;
        if ((link & KOA2_COPY_TAGS) != KOA2_COPY_LINK) return KOA2_FORMAT;
        for (i = 0; i < r->count; i++) if (r->physical[i] == (link & ~KOA2_COPY_TAGS)) found = i;
        if (found != r->page + 1) return KOA2_FORMAT;
        r->page = found; r->word = 0; r->read++;
    }
    *out = r->virtual[r->page][r->word++];
    r->read++;
    return KOA2_OK;
}
static int covered(uint64_t page, const struct koa2_copy_segment *s, size_t count)
{
    size_t i;
    for (i = 0; i < count; i++) {
        uint64_t size = (uint64_t)s[i].page_count * PAGE;
        if (page >= s[i].dest && page - s[i].dest < size) return 1;
    }
    return 0;
}
static int forbidden_hit(uint64_t page, const struct koa2_range *x, size_t n)
{
    struct koa2_range one = { page, PAGE };
    size_t i;
    for (i = 0; i < n; i++) if (koa2_range_overlap(one, x[i])) return 1;
    return 0;
}
int koa2_copylist_audit(const uint32_t *const *list_virtual, const uint32_t *list_physical, size_t list_count,
                        const struct koa2_copy_segment *segments, size_t segment_count,
                        const struct koa2_range *forbidden, size_t forbidden_count,
                        size_t *entries, size_t *pages)
{
    struct reader r;
    uint32_t word;
    size_t i, j, k;
    int rc;
    if (entries) *entries = 0;
    if (pages) *pages = 0;
    if (!list_virtual || !list_physical || !list_count || !segments || !segment_count ||
        (forbidden_count && !forbidden)) return KOA2_BOUNDS;
    if (!list_ok(list_physical, list_count)) return KOA2_RANGE;
    for (i = 0; i < list_count; i++) if (!list_virtual[i]) return KOA2_BOUNDS;
    /* No list page and no source page may lie where the copy writes, or in a
     * range that must survive. This is what makes the copy order irrelevant
     * and keeps the walker from overwriting the list it is reading. */
    for (i = 0; i < list_count; i++) {
        if (covered(list_physical[i], segments, segment_count) ||
            forbidden_hit(list_physical[i], forbidden, forbidden_count)) return KOA2_RANGE;
        for (j = 0; j < segment_count; j++)
            for (k = 0; k < segments[j].page_count; k++)
                if (segments[j].pages[k] == list_physical[i]) return KOA2_RANGE;
    }
    for (i = 0; i < segment_count; i++) {
        if (!page_aligned(segments[i].dest) || !fits32(segments[i].dest) ||
            !segments[i].page_count || !segments[i].pages) return KOA2_RANGE;
        for (j = 0; j < segments[i].page_count; j++) {
            uint64_t page = segments[i].pages[j];
            if (!page || !page_aligned(page)) return KOA2_RANGE;
            if (covered(page, segments, segment_count) ||
                forbidden_hit(page, forbidden, forbidden_count)) return KOA2_RANGE;
            for (k = 0; k < j; k++) if (segments[i].pages[k] == page) return KOA2_RANGE;
        }
    }
    r.virtual = list_virtual; r.physical = list_physical; r.count = list_count;
    r.page = r.word = r.read = 0;
    for (i = 0; i < segment_count; i++) {
        if ((rc = next_word(&r, &word))) return rc;
        if (word != ((uint32_t)segments[i].dest | KOA2_COPY_DEST)) return KOA2_FORMAT;
        for (j = 0; j < segments[i].page_count; j++) {
            if ((rc = next_word(&r, &word))) return rc;
            if (word != (segments[i].pages[j] | KOA2_COPY_SOURCE)) return KOA2_FORMAT;
        }
    }
    if ((rc = next_word(&r, &word))) return rc;
    if (word != KOA2_COPY_DONE) return KOA2_FORMAT;
    /* Nothing after the terminator, and no page of the list left unaccounted. */
    for (i = r.word; i < KOA2_COPY_WORDS; i++) if (list_virtual[r.page][i]) return KOA2_FORMAT;
    if (r.page + 1 != list_count) return KOA2_FORMAT;
    if (entries) *entries = r.read;
    if (pages) *pages = list_count;
    return KOA2_OK;
}
