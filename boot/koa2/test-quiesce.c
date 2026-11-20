/* SPDX-License-Identifier: GPL-2.0-only */
/* The quiesce sequence: that it runs in order, that a failure anywhere puts
 * the stock system back in reverse order, and that the table itself is
 * refused if a reversible step has no way back.
 *
 * What each step does to the hardware needs the device. What this checks is
 * the property the device cannot be asked about safely: if step 6 of 10 fails,
 * is the machine as it was? */
#include "quiesce.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MAX_TRACE 64
struct trace {
    char order[MAX_TRACE];      /* 'r' + step for a run, 'u' + step for an undo */
    unsigned length;
    int fail_at;                /* which step's run fails, -1 for none */
    int undo_fail_at;           /* which step's undo fails, -1 for none */
    int result;
};
static struct trace t;
static void note(char kind, unsigned step)
{
    assert(t.length + 2 < MAX_TRACE);
    t.order[t.length++] = kind;
    t.order[t.length++] = (char)('0' + step);
    t.order[t.length] = 0;
}
#define STEP(n) \
    static int run_##n(void *c) { (void)c; note('r', n); return t.fail_at == (n) ? -5 - (n) : 0; } \
    static int undo_##n(void *c) { (void)c; note('u', n); return t.undo_fail_at == (n) ? -100 : 0; }
STEP(0) STEP(1) STEP(2) STEP(3) STEP(4) STEP(5) STEP(6) STEP(7) STEP(8) STEP(9)
static void fill(struct koa2_quiesce *q)
{
    static const char *const names[KOA2_Q_COUNT] = {
        "validate", "epdc", "wifi", "usb", "sdma", "storage", "second core", "irq", "watchdog", "caches"
    };
    int (*const runs[KOA2_Q_COUNT])(void *) = {
        run_0, run_1, run_2, run_3, run_4, run_5, run_6, run_7, run_8, run_9
    };
    int (*const undos[KOA2_Q_COUNT])(void *) = {
        undo_0, undo_1, undo_2, undo_3, undo_4, undo_5, undo_6, undo_7, undo_8, undo_9
    };
    unsigned i;
    memset(q, 0, sizeof(*q));
    for (i = 0; i < KOA2_Q_COUNT; i++) {
        q->step[i].run = runs[i];
        q->step[i].name = names[i];
        q->step[i].undo = i < KOA2_Q_IRREVERSIBLE ? undos[i] : NULL;
    }
}
static void reset(int fail_at, int undo_fail_at)
{
    memset(&t, 0, sizeof(t));
    t.fail_at = fail_at;
    t.undo_fail_at = undo_fail_at;
}
int main(void)
{
    struct koa2_quiesce q;
    unsigned i;
    /* The table as the handoff will build it. */
    fill(&q);
    assert(KOA2_Q_COUNT == 10 && KOA2_Q_IRREVERSIBLE == KOA2_Q_CACHES);
    assert(koa2_quiesce_check(&q) == KOA2_OK);
    /* All the way through: every step once, in order, nothing undone. */
    reset(-1, -1);
    assert(koa2_quiesce_run(&q) == KOA2_OK);
    assert(!strcmp(t.order, "r0r1r2r3r4r5r6r7r8r9"));
    assert(q.done == KOA2_Q_COUNT && q.undone == 0 && q.failed_step == -1);
    /* A failure at each step: the ones before it are undone in reverse, and
     * the stock system is back. */
    for (i = 0; i < KOA2_Q_COUNT; i++) {
        char want[MAX_TRACE];
        unsigned at = 0, j;
        reset((int)i, -1);
        assert(koa2_quiesce_run(&q) == -5 - (int)i);
        assert(q.failed_step == (int)i && q.failed_result == -5 - (int)i);
        assert(q.done == i);
        for (j = 0; j <= i; j++) { want[at++] = 'r'; want[at++] = (char)('0' + j); }
        j = i;
        while (j--) { want[at++] = 'u'; want[at++] = (char)('0' + j); }
        want[at] = 0;
        assert(!strcmp(t.order, want));
        assert(q.undone == i);
        assert(q.undo_failed_step == -1);
    }
    /* An undo that fails is recorded, and the rest are still attempted: a
     * device that will not give one thing back must not strand the others. */
    reset(5, 2);
    assert(koa2_quiesce_run(&q) == -10);
    assert(!strcmp(t.order, "r0r1r2r3r4r5u4u3u2u1u0"));
    assert(q.undone == 4 && q.undo_failed_step == 2 && q.undo_failed_result == -100);
    /* Tables that must be refused: a missing step, a reversible step with no
     * undo, an undo claimed past the point of no return. */
    fill(&q); q.step[4].run = NULL;
    assert(koa2_quiesce_check(&q) == KOA2_FORMAT);
    fill(&q); q.step[4].name = NULL;
    assert(koa2_quiesce_check(&q) == KOA2_FORMAT);
    fill(&q); q.step[3].undo = NULL;
    assert(koa2_quiesce_check(&q) == KOA2_FORMAT);
    reset(-1, -1);
    assert(koa2_quiesce_run(&q) == KOA2_FORMAT && !t.length);
    fill(&q); q.step[KOA2_Q_CACHES].undo = undo_9;
    assert(koa2_quiesce_check(&q) == KOA2_FORMAT);
    assert(koa2_quiesce_check(NULL) == KOA2_BOUNDS && koa2_quiesce_run(NULL) == KOA2_BOUNDS);
    puts("KOA2 quiesce: order, rollback in reverse from every step, a failing undo, and tables with no way back refused");
    return 0;
}
