/*
 * HealthyPi Move — Health Store: epoch aggregation (HS-2 P1)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * Rationale and the per-signal statistic table: see hpi_hs_epoch.h and
 * docs/HS_SYNC_REDESIGN_PLAN.md §2.2.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "health/hpi_hs_types.h"
#include "health/hpi_health_store.h"
#include "health/hpi_hs_epoch.h"

LOG_MODULE_REGISTER(hpi_hs_epoch, LOG_LEVEL_INF);

/* Epoch lengths. HR is 1 min (it moves fast enough that a longer window would
 * blur real structure); skin temp is 5 min (it does not). These are the values
 * §2.4 of the plan says to VALIDATE against real wear rather than assume — the
 * spread logging below is what validates them. */
/* Defaulted so this module compiles standalone in the unit test (which does not
 * pull in the app's Kconfig). The real build always supplies these. */
#if defined(CONFIG_HPI_HS_EPOCH_HR_S)
#define HS_EPOCH_HR_S       CONFIG_HPI_HS_EPOCH_HR_S
#else
#define HS_EPOCH_HR_S       60
#endif
#if defined(CONFIG_HPI_HS_EPOCH_TEMP_S)
#define HS_EPOCH_TEMP_S     CONFIG_HPI_HS_EPOCH_TEMP_S
#else
#define HS_EPOCH_TEMP_S     300
#endif
#if defined(CONFIG_HPI_HS_EPOCH_COUNTER_S)
#define HS_EPOCH_COUNTER_S  CONFIG_HPI_HS_EPOCH_COUNTER_S
#else
#define HS_EPOCH_COUNTER_S  60
#endif

enum hs_epoch_kind {
    HS_EK_SPIKY = 0,   /* mean + min + max            (HR)            */
    HS_EK_LEVEL,       /* mean + count                (skin temp)     */
    HS_EK_COUNTER,     /* last value in window        (steps, energy) */
};

enum hs_epoch_slot {
    HS_SLOT_HR = 0,
    HS_SLOT_TEMP,
    HS_SLOT_STEPS,
    HS_SLOT_ENERGY,
    HS_SLOT_N
};

struct hs_epoch_cfg {
    uint8_t  kind;
    uint8_t  t_primary;   /* mean (SPIKY/LEVEL) or last value (COUNTER) */
    uint8_t  t_min;       /* 0 = not emitted */
    uint8_t  t_max;       /* 0 = not emitted */
    uint8_t  t_count;     /* 0 = not emitted */
    uint16_t epoch_s;
    const char *name;     /* spread logging only */
};

static const struct hs_epoch_cfg s_cfg[HS_SLOT_N] = {
    [HS_SLOT_HR]     = {HS_EK_SPIKY,   HPI_HS_T_HR,            HPI_HS_T_HR_MIN, HPI_HS_T_HR_MAX,
                        0,                      HS_EPOCH_HR_S,      "hr"},
    [HS_SLOT_TEMP]   = {HS_EK_LEVEL,   HPI_HS_T_SKIN_TEMP,     0, 0,
                        HPI_HS_T_SKIN_TEMP_CNT, HS_EPOCH_TEMP_S,    "temp"},
    [HS_SLOT_STEPS]  = {HS_EK_COUNTER, HPI_HS_T_STEPS,         0, 0,
                        0,                      HS_EPOCH_COUNTER_S, "steps"},
    [HS_SLOT_ENERGY] = {HS_EK_COUNTER, HPI_HS_T_ACTIVE_ENERGY, 0, 0,
                        0,                      HS_EPOCH_COUNTER_S, "energy"},
};

struct hs_epoch_st {
    bool    open;
    int64_t win;        /* wall-clock window index = ts_utc / epoch_s */
    int64_t sum;        /* int64: headroom, though int32 would do */
    int32_t count;
    int32_t vmin, vmax;
    int32_t last;       /* COUNTER: latest cumulative value seen */
    int64_t last_ts;    /* COUNTER: ts of `last`, used when force-closing on reset */
    uint8_t q_and;      /* AND of the window's sample qualities (conservative) */
};

static struct hs_epoch_st s_st[HS_SLOT_N];
static K_MUTEX_DEFINE(s_epoch_lock);

/* Emit the closed epoch. Caller holds the lock; hpi_hs_record() takes its own. */
static void epoch_emit(enum hs_epoch_slot slot)
{
    const struct hs_epoch_cfg *c = &s_cfg[slot];
    struct hs_epoch_st *st = &s_st[slot];

    if (!st->open) {
        return;
    }

    if (c->kind == HS_EK_COUNTER) {
        /* Cumulative: the ONLY meaningful epoch statistic is the last value in the
         * window. max == last (monotonic within a day) and the mean of a monotonic
         * ramp is meaningless. Timestamp with the sample's own ts, not the window
         * end — on a force-close (midnight reset) the window end lies in the future. */
        hpi_hs_record(c->t_primary, st->last, st->q_and, st->last_ts);
    } else if (st->count > 0) {
        /* Wall-clock end of the window: the epoch describes [win, win+1). */
        int64_t ts_end = (st->win + 1) * (int64_t)c->epoch_s;
        int32_t mean = (int32_t)(st->sum / st->count);

        hpi_hs_record(c->t_primary, mean, st->q_and, ts_end);

        if (c->t_min) {
            hpi_hs_record(c->t_min, st->vmin, st->q_and, ts_end);
        }
        if (c->t_max) {
            hpi_hs_record(c->t_max, st->vmax, st->q_and, ts_end);
        }
        if (c->t_count) {
            hpi_hs_record(c->t_count, st->count, st->q_and, ts_end);
        }

#if defined(CONFIG_HPI_HS_EPOCH_SPREAD_LOG)
        /* §2.4: the epoch lengths above rest on an ASSUMPTION about how much each
         * signal moves inside a window. This is the measurement that checks it. If
         * HR spread is routinely tiny, the HR epoch can grow (saving ~5x more). If
         * TEMP spread is routinely large, the "temp is slow" premise is wrong and
         * temp needs min/max after all.
         *
         * Track the running max/mean spread and log only every 60th epoch: the
         * synthetic generator closes ~12,000 epochs in ~100 s, and logging each one
         * floods the backend (which is what wedged the first attempt). The rolling
         * stats below carry the same information without the flood. */
        static uint32_t s_ep_n[HS_SLOT_N];
        static int32_t  s_ep_spread_max[HS_SLOT_N];
        static int64_t  s_ep_spread_sum[HS_SLOT_N];
        int32_t spread = st->vmax - st->vmin;

        s_ep_n[slot]++;
        s_ep_spread_sum[slot] += spread;
        if (spread > s_ep_spread_max[slot]) {
            s_ep_spread_max[slot] = spread;
        }
        if ((s_ep_n[slot] % 60u) == 0u) {
            LOG_INF("SPREAD %s: n=%u  spread avg=%d max=%d  (last: mean=%d min=%d max=%d)",
                    c->name, s_ep_n[slot],
                    (int32_t)(s_ep_spread_sum[slot] / s_ep_n[slot]),
                    s_ep_spread_max[slot], mean, st->vmin, st->vmax);
        }
#endif
    }

    st->open = false;
}

/* Open a fresh window for `value` at `ts`. Caller holds the lock. */
static void epoch_open(enum hs_epoch_slot slot, int32_t value, uint8_t q, int64_t ts)
{
    const struct hs_epoch_cfg *c = &s_cfg[slot];
    struct hs_epoch_st *st = &s_st[slot];

    st->open    = true;
    st->win     = ts / (int64_t)c->epoch_s;
    st->sum     = value;
    st->count   = 1;
    st->vmin    = value;
    st->vmax    = value;
    st->last    = value;
    st->last_ts = ts;
    st->q_and   = q;
}

static void epoch_feed(enum hs_epoch_slot slot, int32_t value, uint8_t q, int64_t ts)
{
    const struct hs_epoch_cfg *c = &s_cfg[slot];
    struct hs_epoch_st *st = &s_st[slot];

    /* Same gate hpi_hs_record() applies: a sample with no valid timestamp has no
     * meaning in a UTC-native store, and would corrupt the window index. */
    if (!(q & HPI_HS_Q_VALID)) {
        return;
    }

    k_mutex_lock(&s_epoch_lock, K_FOREVER);

    if (!st->open) {
        epoch_open(slot, value, q, ts);
        k_mutex_unlock(&s_epoch_lock);
        return;
    }

    /* CUMULATIVE reset (local midnight, e.g. 8000 -> 0). Close the window NOW,
     * emitting the pre-reset peak, before the counter rolls. Without this the
     * day's final total is LOST whenever the reset lands mid-window — a silent
     * data-loss bug that rate-limiting would INTRODUCE (today every change is
     * recorded, so 8000 then 0 both survive). */
    if (c->kind == HS_EK_COUNTER && value < st->last) {
        LOG_INF("epoch %s: counter reset %d -> %d, flushing pre-reset peak",
                c->name, st->last, value);
        epoch_emit(slot);
        epoch_open(slot, value, q, ts);
        k_mutex_unlock(&s_epoch_lock);
        return;
    }

    int64_t win = ts / (int64_t)c->epoch_s;
    if (win != st->win) {
        epoch_emit(slot);          /* window rolled */
        epoch_open(slot, value, q, ts);
        k_mutex_unlock(&s_epoch_lock);
        return;
    }

    /* accumulate into the open window */
    st->sum += value;
    st->count++;
    if (value < st->vmin) {
        st->vmin = value;
    }
    if (value > st->vmax) {
        st->vmax = value;
    }
    st->last    = value;
    st->last_ts = ts;
    st->q_and  &= q;   /* conservative: the epoch claims a context bit only if EVERY
                        * sample in it had that bit (e.g. ON_SKIN for the whole window) */

    k_mutex_unlock(&s_epoch_lock);
}

void hpi_hs_epoch_hr(int32_t bpm, uint8_t quality, int64_t ts_utc)
{
    epoch_feed(HS_SLOT_HR, bpm, quality, ts_utc);
}

void hpi_hs_epoch_temp(int32_t temp_c_x100, uint8_t quality, int64_t ts_utc)
{
    epoch_feed(HS_SLOT_TEMP, temp_c_x100, quality, ts_utc);
}

void hpi_hs_epoch_steps(int32_t cumulative, uint8_t quality, int64_t ts_utc)
{
    epoch_feed(HS_SLOT_STEPS, cumulative, quality, ts_utc);
}

void hpi_hs_epoch_energy(int32_t cumulative, uint8_t quality, int64_t ts_utc)
{
    epoch_feed(HS_SLOT_ENERGY, cumulative, quality, ts_utc);
}

void hpi_hs_epoch_tick(int64_t now_utc)
{
    k_mutex_lock(&s_epoch_lock, K_FOREVER);
    for (int i = 0; i < HS_SLOT_N; i++) {
        struct hs_epoch_st *st = &s_st[i];
        if (!st->open) {
            continue;
        }
        /* Close a window whose time has passed. Without this an epoch stays open
         * forever once its signal stops (watch taken off mid-window) and is never
         * written. */
        if (now_utc / (int64_t)s_cfg[i].epoch_s > st->win) {
            epoch_emit((enum hs_epoch_slot)i);
        }
    }
    k_mutex_unlock(&s_epoch_lock);
}

void hpi_hs_epoch_flush_all(int64_t now_utc)
{
    ARG_UNUSED(now_utc);
    k_mutex_lock(&s_epoch_lock, K_FOREVER);
    for (int i = 0; i < HS_SLOT_N; i++) {
        epoch_emit((enum hs_epoch_slot)i);
    }
    k_mutex_unlock(&s_epoch_lock);
}
