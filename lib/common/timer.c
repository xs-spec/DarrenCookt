/*
 * Copyright (c) 2017 Fastly, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <sys/time.h>
#include "h2o/memory.h"
#include "h2o/socket.h"

#define H2O_TIMERWHEEL_SLOTS_MASK (H2O_TIMERWHEEL_SLOTS_PER_WHEEL - 1)
#define H2O_TIMERWHEEL_MAX_TIMER(num_wheels) ((1LU << (H2O_TIMERWHEEL_BITS_PER_WHEEL * (num_wheels))) - 1)

#ifndef H2O_TIMER_VALIDATE
#define H2O_TIMER_VALIDATE 0
#endif

struct st_h2o_timer_wheel_t {
    /**
     * the last time h2o_timer_run_wheel was called
     */
    uint64_t last_run;
    /**
     * maximum ticks that can be retained safely in the structure. Objects that need to be retained longer will be re-registered at
     * the highest wheel.
     */
    uint64_t max_ticks;
    /**
     * number of wheels and the wheel
     */
    size_t num_wheels;
    h2o_linklist_t wheel[1][H2O_TIMERWHEEL_SLOTS_PER_WHEEL];
};

void h2o_timer_dump_wheel(h2o_timer_wheel_t *w)
{
    size_t wheel, slot;

    fprintf(stderr, "%s(%p):\n", __FUNCTION__, w);
    for (wheel = 0; wheel < w->num_wheels; wheel++) {
        for (slot = 0; slot < H2O_TIMERWHEEL_SLOTS_PER_WHEEL; slot++) {
            h2o_linklist_t *anchor = &w->wheel[wheel][slot], *l;
            for (l = anchor->next; l != anchor; l = l->next) {
                h2o_timer_t *t = H2O_STRUCT_FROM_MEMBER(h2o_timer_t, _link, l);
                fprintf(stderr, "  - {wheel: %zu, slot: %zu, expires:%" PRIu64 ", self: %p, cb:%p}\n", wheel, slot, t->expire_at, t,
                        t->cb);
            }
        }
    }
}

/* timer APIs */

static size_t timer_wheel(size_t num_wheels, uint64_t delta)
{
    delta &= H2O_TIMERWHEEL_MAX_TIMER(num_wheels);
    if (delta == 0)
        return 0;

    H2O_BUILD_ASSERT(sizeof(unsigned long long) == 8);
    return (63 - __builtin_clzll(delta)) / H2O_TIMERWHEEL_BITS_PER_WHEEL;
}

/* calculate slot number based on the absolute expiration time */
static size_t timer_slot(size_t wheel, uint64_t expire)
{
    return H2O_TIMERWHEEL_SLOTS_MASK & (expire >> (wheel * H2O_TIMERWHEEL_BITS_PER_WHEEL));
}

/**
 * returned at_max is inclusive
 */
static void calc_expire_for_slot(size_t num_wheels, uint64_t last_run, size_t wheel_index, size_t slot_index, uint64_t *at_min,
                                 uint64_t *at_max)
{
#define SPAN(i) ((uint64_t)1 << (H2O_TIMERWHEEL_BITS_PER_WHEEL * (i))) /* returns the span of time for given wheel index */

    int adj_at_min = 0;

    *at_min = (last_run & ~(SPAN(wheel_index + 1) - 1)) + slot_index * SPAN(wheel_index);

    if (wheel_index == 0) {
        if (*at_min < last_run)
            adj_at_min = 1;
    } else {
        if (*at_min <= last_run)
            adj_at_min = 1;
    }
    if (adj_at_min)
        *at_min += SPAN(wheel_index + 1);

    if (wheel_index == num_wheels - 1) {
        *at_max = UINT64_MAX;
    } else {
        *at_max = *at_min + SPAN(wheel_index) - 1;
    }

#undef SPAN
}

static int validate_slot(h2o_timer_wheel_t *wheel, size_t wheel_index, size_t slot_index)
{
    h2o_linklist_t *anchor = &wheel->wheel[wheel_index][slot_index], *link;
    uint64_t at_min, at_max;
    int success = 1;

    calc_expire_for_slot(wheel->num_wheels, wheel->last_run, wheel_index, slot_index, &at_min, &at_max);

    for (link = anchor->next; link != anchor; link = link->next) {
        h2o_timer_t *timer = H2O_STRUCT_FROM_MEMBER(h2o_timer_t, _link, link);
        if (!(at_min <= timer->expire_at && timer->expire_at <= at_max)) {
            fprintf(stderr, "invalid entry at %zu,%zu; last_run=%" PRIu64 ", expire_at=%" PRIu64 " (expected range: [%" PRIu64
                            ",%" PRIu64 "])\n",
                    wheel_index, slot_index, wheel->last_run, timer->expire_at, at_min, at_max);
            success = 0;
        }
    }

    return success;
}

int h2o_timer_validate_wheel(h2o_timer_wheel_t *wheel)
{
    size_t wheel_index, slot_index;
    int success = 1;

    for (wheel_index = 0; wheel_index < wheel->num_wheels; ++wheel_index)
        for (slot_index = 0; slot_index < H2O_TIMERWHEEL_SLOTS_PER_WHEEL; ++slot_index)
            if (!validate_slot(wheel, wheel_index, slot_index))
                success = 0;

    return success;
}

uint64_t h2o_timer_get_wake_at(h2o_timer_wheel_t *w)
{
#if H2O_TIMER_VALIDATE
    assert(h2o_timer_validate_wheel(w));
#endif

    size_t wheel_index, slot_index;
    uint64_t at = w->last_run;

    for (wheel_index = 0; wheel_index < w->num_wheels; ++wheel_index) {
        uint64_t at_incr = (uint64_t)1 << (wheel_index * H2O_TIMERWHEEL_BITS_PER_WHEEL);
        size_t slot_base = timer_slot(wheel_index, at);
        /* check current wheel from slot_base */
        for (slot_index = slot_base; slot_index < H2O_TIMERWHEEL_SLOTS_PER_WHEEL; ++slot_index) {
            if (!h2o_linklist_is_empty(&w->wheel[wheel_index][slot_index]))
                goto Found;
            at += at_incr;
        }
        while (1) {
            /* handle carry */
            if (wheel_index + 1 < w->num_wheels) {
                size_t wi;
                for (wi = wheel_index + 1; wi < w->num_wheels; ++wi) {
                    size_t si = timer_slot(wi, at);
                    if (!h2o_linklist_is_empty(&w->wheel[wi][si]))
                        goto Found;
                    if (si != 0)
                        break;
                }
            }
            /* check current wheel from 0 to slot_base */
            if (slot_base == 0)
                break;
            for (slot_index = 0; slot_index < slot_base; ++slot_index) {
                if (!h2o_linklist_is_empty(&w->wheel[wheel_index][slot_index]))
                    goto Found;
                at += at_incr;
            }
            at += at_incr * (H2O_TIMERWHEEL_SLOTS_PER_WHEEL - slot_base);
            slot_base = 0;
        }
    }

    /* not found */
    return UINT64_MAX;
Found:
    return at;
}

static void link_timer(h2o_timer_wheel_t *w, h2o_timer_t *timer)
{
    h2o_linklist_t *slot;
    size_t wid, sid;
    uint64_t wheel_abs = timer->expire_at;

    if (wheel_abs > w->last_run + w->max_ticks)
        wheel_abs = w->last_run + w->max_ticks;

    wid = timer_wheel(w->num_wheels, wheel_abs - w->last_run);
    sid = timer_slot(wid, wheel_abs);
    {
        uint64_t at_min, at_max;
        calc_expire_for_slot(w->num_wheels, w->last_run, wid, sid, &at_min, &at_max);
        if (!(at_min <= timer->expire_at && timer->expire_at <= at_max)) {
            fprintf(stderr, "%s:last_run=%" PRIu64 ",expire_at=%" PRIu64 ",wid=%zu,sid=%zu,at_min=%" PRIu64 ",at_max=%" PRIu64 "\n",
                    __FUNCTION__, w->last_run, timer->expire_at, wid, sid, at_min, at_max);
            abort();
        }
    }
    slot = &w->wheel[wid][sid];

    h2o_linklist_insert(slot, &timer->_link);
}

/* timer wheel APIs */

/**
 * initializes a timerwheel
 */
h2o_timer_wheel_t *h2o_timer_create_wheel(size_t num_wheels, uint64_t now)
{
    h2o_timer_wheel_t *w = h2o_mem_alloc(offsetof(h2o_timer_wheel_t, wheel) + sizeof(w->wheel[0]) * num_wheels);
    size_t i, j;

    w->last_run = now;
    /* max_ticks cannot be so large that the entry will be linked once more to the same slot, see the assert in `cascade` */
    w->max_ticks = ((uint64_t)1 << (H2O_TIMERWHEEL_BITS_PER_WHEEL * (num_wheels - 1))) * (H2O_TIMERWHEEL_SLOTS_PER_WHEEL - 1);
    w->num_wheels = num_wheels;
    for (i = 0; i < w->num_wheels; i++)
        for (j = 0; j < H2O_TIMERWHEEL_SLOTS_PER_WHEEL; j++)
            h2o_linklist_init_anchor(&w->wheel[i][j]);

    return w;
}

void h2o_timer_destroy_wheel(h2o_timer_wheel_t *w)
{
    size_t i, j;

    for (i = 0; i < w->num_wheels; ++i) {
        for (j = 0; j < H2O_TIMERWHEEL_SLOTS_PER_WHEEL; ++j) {
            while (!h2o_linklist_is_empty(&w->wheel[i][j])) {
                h2o_timer_t *entry = H2O_STRUCT_FROM_MEMBER(h2o_timer_t, _link, w->wheel[i][j].next);
                h2o_timer_unlink(entry);
            }
        }
    }

    free(w);
}

/**
 * cascading happens when the lower wheel wraps around and ticks the next
 * higher wheel
 */
static void cascade_one(h2o_timer_wheel_t *w, size_t wheel, size_t slot)
{
    assert(wheel > 0);

    h2o_linklist_t *s = &w->wheel[wheel][slot];

    while (!h2o_linklist_is_empty(s)) {
        h2o_timer_t *entry = H2O_STRUCT_FROM_MEMBER(h2o_timer_t, _link, s->next);
        assert(w->last_run <= entry->expire_at);
        h2o_linklist_unlink(&entry->_link);
        link_timer(w, entry);
        assert(&entry->_link != s->prev); /* detect the entry reassigned to the same slot */
    }
}

static int cascade_all(h2o_timer_wheel_t *w, size_t wheel)
{
    int cascaded = 0;

    for (; wheel < w->num_wheels; ++wheel) {
        size_t slot = timer_slot(wheel, w->last_run);
        if (!h2o_linklist_is_empty(&w->wheel[wheel][slot]))
            cascaded = 1;
        cascade_one(w, wheel, slot);
        if (slot != 0)
            break;
    }

    return cascaded;
}

size_t h2o_timer_run_wheel(h2o_timer_wheel_t *w, uint64_t now)
{
    h2o_linklist_t todo;
    size_t wheel_index = 0, slot_index, slot_start, count = 0;

#if H2O_TIMER_VALIDATE
    assert(h2o_timer_validate_wheel(w));
#endif

    /* time might rewind if the clock is reset */
    if (now < w->last_run)
        return 0;

    h2o_linklist_init_anchor(&todo);

Redo:
    /* collect from the first slot */
    slot_start = timer_slot(wheel_index, w->last_run);
    for (slot_index = slot_start; slot_index < H2O_TIMERWHEEL_SLOTS_PER_WHEEL; ++slot_index) {
        if (wheel_index == 0) {
            h2o_linklist_insert_list(&todo, w->wheel[wheel_index] + slot_index);
            if (w->last_run == now)
                goto Collected;
            ++w->last_run;
        } else {
            if (!h2o_linklist_is_empty(&w->wheel[wheel_index][slot_index])) {
                cascade_one(w, wheel_index, slot_index);
                assert(h2o_linklist_is_empty(&w->wheel[wheel_index][slot_index]));
                wheel_index = 0;
                goto Redo;
            }
            w->last_run += 1 << (wheel_index * H2O_TIMERWHEEL_BITS_PER_WHEEL);
            if (w->last_run > now) {
                w->last_run = now;
                goto Collected;
            }
        }
    }
    /* carry */
    if (cascade_all(w, wheel_index + 1)) {
        wheel_index = 0;
        goto Redo;
    }
    if (slot_start != 0 || ++wheel_index < w->num_wheels)
        goto Redo;
    /* all the wheels were empty, and they all belonged to the past */
    if (w->last_run < now)
        w->last_run = now;

Collected: /* expiration processing */
    assert(w->last_run == now);
    while (!h2o_linklist_is_empty(&todo)) {
        do {
            h2o_timer_t *timer = H2O_STRUCT_FROM_MEMBER(h2o_timer_t, _link, todo.next);
            /* remove this timer from todo list */
            h2o_linklist_unlink(&timer->_link);
            if (timer->expire_at <= now) {
                timer->cb(timer);
                count++;
            } else {
                link_timer(w, timer);
            }
        } while (!h2o_linklist_is_empty(&todo));
        h2o_linklist_insert_list(&todo, w->wheel[0] + timer_slot(0, now));
    }

#if H2O_TIMER_VALIDATE
    assert(h2o_timer_validate_wheel(w));
#endif

    return count;
}

void h2o_timer_link_abs(h2o_timer_wheel_t *w, h2o_timer_t *timer, uint64_t at)
{
    timer->expire_at = at < w->last_run ? w->last_run : at;
    link_timer(w, timer);
}
