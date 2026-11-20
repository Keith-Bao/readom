/* SPDX-License-Identifier: GPL-2.0-only */
#include "quiesce.h"
int koa2_quiesce_check(const struct koa2_quiesce *q)
{
    unsigned i;
    if (!q) return KOA2_BOUNDS;
    for (i = 0; i < KOA2_Q_COUNT; i++) {
        if (!q->step[i].run || !q->step[i].name) return KOA2_FORMAT;
        /* A reversible step without an undo would leave the stock system
         * changed with no way back; an undo past the point of no return would
         * claim one that does not exist. */
        if ((i < KOA2_Q_IRREVERSIBLE) != (q->step[i].undo != NULL)) return KOA2_FORMAT;
    }
    return KOA2_OK;
}
int koa2_quiesce_run(struct koa2_quiesce *q)
{
    unsigned i;
    int rc;
    if ((rc = koa2_quiesce_check(q))) return rc;
    q->done = q->undone = 0;
    q->failed_step = q->undo_failed_step = -1;
    q->failed_result = q->undo_failed_result = 0;
    for (i = 0; i < KOA2_Q_COUNT; i++) {
        rc = q->step[i].run(q->context);
        if (rc) {
            q->failed_step = (int)i;
            q->failed_result = rc;
            break;
        }
        q->done = i + 1;
    }
    if (q->failed_step < 0) return KOA2_OK;
    /* Undo what completed, in reverse. Nothing at or past the point of no
     * return can have completed: the loop breaks at the first failure, and a
     * failure there would mean the caches were already off, which the caller
     * cannot come back from -- so koa2_quiesce_check refuses an undo for those
     * steps and this loop never looks for one. */
    i = q->done;
    while (i--) {
        int undo;
        if (i >= KOA2_Q_IRREVERSIBLE) continue;
        undo = q->step[i].undo(q->context);
        if (undo) {
            if (q->undo_failed_step < 0) {
                q->undo_failed_step = (int)i;
                q->undo_failed_result = undo;
            }
            continue;
        }
        q->undone++;
    }
    return q->failed_result;
}
