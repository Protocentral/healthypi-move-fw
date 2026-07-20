/*
 * HealthyPi Move — Health Store: synthetic data generator (test only)
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Protocentral Electronics
 *
 * See hpi_hs_synth.h. Gated by CONFIG_HPI_HS_SYNTH.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "health/hpi_hs_types.h"
#include "health/hpi_health_store.h"
#include "health/hpi_hs_epoch.h"
#include "health/hpi_hs_stress.h"
#include "health/hpi_hs_synth.h"
#include "hpi_sys.h"
#include "hw_module.h"

LOG_MODULE_REGISTER(hpi_hs_synth, LOG_LEVEL_INF);

#define DAY_S    86400
#define HR_DT    3      /* real HR publish cadence, s   */
#define TEMP_DT  5      /* real temp publish cadence, s */
#define STEP_DT  5      /* real steps publish cadence   */
#define HRV_WIN_S 300   /* must match CONFIG_HPI_HS_HRV_WINDOW_S */

/* Every synthetic sample is branded. Never mistakable for a measurement. */
#define QS  (HPI_HS_Q_VALID | HPI_HS_Q_SYNTHETIC)

/* Deterministic PRNG — a fixed seed means a reproducible dataset, so a failing run
 * can be replayed exactly. (Math.random-style nondeterminism would make a trend bug
 * unreproducible.) */
static uint32_t s_rng = 0x13579BDFu;

static int32_t rnd(int32_t span)   /* uniform in [-span, +span] */
{
    s_rng = s_rng * 1664525u + 1013904223u;
    if (span == 0) {
        return 0;
    }
    return (int32_t)((s_rng >> 8) % (uint32_t)(2 * span + 1)) - span;
}

/* Asleep 23:00-07:00. */
static inline bool asleep(int t)
{
    return (t >= 23 * 3600) || (t < 7 * 3600);
}

/* Watch OFF the wrist 20:00-21:30 (on the charger): no samples at all. This is what
 * makes gaps, partial epochs and the quality AND real rather than theoretical. */
static inline bool off_skin(int t)
{
    return (t >= 20 * 3600) && (t < 21 * 3600 + 1800);
}

/* Two exercise bouts a day: 07:30-08:00 and 18:00-18:40. THE reason HR_MIN/HR_MAX
 * exist — a mean-only minute-epoch would blur a 150 bpm peak down to ~90. */
static inline bool exercising(int t)
{
    return ((t >= 7 * 3600 + 1800) && (t < 8 * 3600)) ||
           ((t >= 18 * 3600) && (t < 18 * 3600 + 2400));
}

/* Circadian HR: ~50 asleep, 65-80 awake, spiking during a bout. */
static int32_t synth_hr(int t)
{
    if (exercising(t)) {
        /* ramp up and back down across the bout, peaking ~150-160 */
        return 120 + (rnd(20) + 20) + rnd(8);
    }
    if (asleep(t)) {
        return 50 + rnd(4);
    }
    /* gentle diurnal drift 65..80 */
    int32_t base = 66 + ((t / 3600) % 12);
    return base + rnd(5);
}

/* Skin temp x100. Nocturnal RISE (wrist skin temp goes up in sleep), ~33-35 degC.
 * Slow-moving by construction, which is the premise behind temp getting mean+count
 * and no min/max — if this assumption is wrong, the spread log will say so. */
static int32_t synth_temp_x100(int t, uint32_t day)
{
    int32_t base = asleep(t) ? 3480 : 3350;
    base += (int32_t)(day % 3) * 6;    /* a slow multi-day drift, so the 7-day
                                        * baseline and the deviation are non-trivial */
    return base + rnd(12);
}

/* HRV recovers slowly after a bout: RMSSD stays suppressed well past the point where
 * HR itself is back to normal. This is the whole reason HRV is worth having -- it sees
 * a load that HR alone has already stopped reporting. */
static inline bool post_exercise(int t)
{
    return ((t >= 8 * 3600) && (t < 8 * 3600 + 5400)) ||
           ((t >= 18 * 3600 + 2400) && (t < 18 * 3600 + 2400 + 5400));
}

/* RMSSD (ms x10), anti-correlated with strain: high asleep, moderate at rest, strongly
 * suppressed after a bout. Randomness alone would NOT do here -- with a noise-only
 * RMSSD the stress score is a flat line, and a completely broken stress pipeline would
 * look exactly like a working one. */
static inline int mid_of(int ws) { return ws + HRV_WIN_S / 2; }

static int32_t synth_rmssd_x10(int t, uint32_t day, uint32_t days)
{
    int32_t v;

    if (asleep(t)) {
        v = 600;                     /* 60 ms: parasympathetic, deep rest */
    } else if (post_exercise(t)) {
        v = 260;                     /* 26 ms: suppressed hours after the bout */
    } else {
        v = 420;                     /* 42 ms: ordinary waking rest */
    }

    /* TODAY is deliberately a high-strain day: RMSSD ~30% below the user's own norm,
     * so `stress_hrv` lands clearly ABOVE the neutral 50 rather than on it. That is a
     * test-design choice, not physiology -- a fixture whose correct answer is exactly
     * the midpoint cannot distinguish a working score from a stub that returns the
     * midpoint. The 6 prior days build the baseline it is measured against. */
    if (day == days - 1) {
        v = (v * 7) / 10;
    }

    v += rnd(50);
    return (v < 80) ? 80 : v;
}

int hpi_hs_synth_generate(uint32_t days, int64_t end_utc)
{
    if (days == 0 || days > 30) {
        return -EINVAL;
    }
#if defined(CONFIG_HPI_HS_SYNTH_RTC_BOOTSTRAP)
    /* A bench board's RTC is unset (it reads year 2000), so hpi_sys_is_time_valid()
     * is false and SUMMARY's "today" would never match the synthetic data. The debug
     * UART is TX-only, so the time cannot be typed in either. Set a fixed date --
     * which also makes the dataset reproducible run to run. */
    if (!hpi_sys_is_time_valid()) {
        LOG_WRN("synth: RTC invalid - bootstrapping to a fixed test date "
                "(12:00:00 2026-07-12)");
        hw_rtc_set_time(0, 0, 12, 12, 7, 26);   /* ss, mm, hh, dd, mon, yy */
        k_sleep(K_MSEC(1200));                  /* let hpi_sys re-sync its offset */
    }
#endif

    if (end_utc <= 0) {
        end_utc = hw_get_sys_time_ts();
    }
    if (!hpi_sys_is_time_valid() || end_utc <= 0) {
        LOG_ERR("synth: RTC still invalid (ts=%lld) - refusing to fabricate "
                "out-of-range timestamps", (long long)end_utc);
        return -EINVAL;
    }

    /* Anchor on TODAY's midnight and generate backwards, so the final (current) day
     * runs up to `end_utc` rather than stopping at the previous midnight.
     *
     * Getting this wrong makes the whole test misleading: SUMMARY works on "today",
     * so if the data ends at last midnight then today is EMPTY -- steps_today reads 0
     * (the last record written is the midnight reset itself), the resting-HR and
     * temp-deviation windows are half-populated, and the numbers look like a bug in
     * P1 when they are really a bug in the fixture. */
    int64_t today0 = end_utc - (end_utc % DAY_S);          /* midnight of the current day */
    int64_t start  = today0 - (int64_t)(days - 1) * DAY_S; /* first full day              */

    int fed = 0;
    int hrv_wins = 0;

    LOG_WRN("synth: generating %u day(s) of SYNTHETIC data (ts %lld..%lld) - "
            "every sample carries HPI_HS_Q_SYNTHETIC",
            days, (long long)start, (long long)end_utc);

    /* Walk HOUR BY HOUR, not signal-by-signal across the whole day.
     *
     * This is not cosmetic. The generator runs in the store thread, which is also
     * the only thing that drains the RAM ring to flash. A day emits ~5,400 records
     * into a 512-entry ring, so generating a whole day before flushing would WRAP
     * the ring and silently lose all but the last 512 records -- the test would
     * "pass" against almost no data. One hour emits ~265 records, comfortably under
     * the ring, and we flush at the end of each one.
     *
     * Interleaving the signals by hour also keeps ts roughly monotonic in seq order,
     * which is what a real device produces. */
    for (uint32_t d = 0; d < days; d++) {
        int64_t day0 = start + (int64_t)d * DAY_S;
        int32_t steps = 0;
        /* The last day is TODAY and is only partial -- stop at `now`. */
        int day_end = (day0 + DAY_S <= end_utc) ? DAY_S : (int)(end_utc - day0);

        if (day_end <= 0) {
            continue;
        }

        for (int h = 0; h * 3600 < day_end; h++) {
            int h0 = h * 3600;
            int h1 = MIN(h0 + 3600, day_end);

            /* ---- HR: every 3 s (the real publish cadence) ---- */
            for (int t = h0; t < h1; t += HR_DT) {
                if (off_skin(t)) {
                    continue;              /* on the charger: nothing published */
                }
                uint8_t q = QS | HPI_HS_Q_ON_SKIN;
                if (asleep(t)) {
                    q |= HPI_HS_Q_DURING_SLEEP | HPI_HS_Q_LOW_MOTION;
                } else if (!exercising(t)) {
                    q |= HPI_HS_Q_LOW_MOTION;
                }
                hpi_hs_epoch_hr(synth_hr(t), q, day0 + t);
                fed++;
            }

            /* ---- skin temp: every 5 s ---- */
            for (int t = h0; t < h1; t += TEMP_DT) {
                if (off_skin(t)) {
                    continue;
                }
                uint8_t q = QS | HPI_HS_Q_ON_SKIN;
                if (asleep(t)) {
                    q |= HPI_HS_Q_DURING_SLEEP;
                }
                hpi_hs_epoch_temp(synth_temp_x100(t, d), q, day0 + t);
                fed++;
            }

            /* ---- steps: CUMULATIVE, accumulating while awake ---- */
            for (int t = h0; t < h1; t += STEP_DT) {
                if (asleep(t) || off_skin(t)) {
                    continue;
                }
                int32_t inc = exercising(t) ? (8 + rnd(3)) : (rnd(2) > 0 ? 2 + rnd(2) : 0);
                if (inc <= 0) {
                    continue;
                }
                steps += inc;
                hpi_hs_epoch_steps(steps, QS, day0 + t);
                fed++;
            }

            /* ---- sparse spot checks, so the event types are populated ---- */
            if (h == 9) {
                hpi_hs_record(HPI_HS_T_SPO2, 96 + rnd(2), QS | HPI_HS_Q_MANUAL, day0 + h0);
                fed++;
            } else if (h == 10) {
                hpi_hs_record(HPI_HS_T_ECG_HR, 68 + rnd(6), QS | HPI_HS_Q_MANUAL, day0 + h0);
                hpi_hs_record(HPI_HS_T_BP_SYS, 118 + rnd(8), QS | HPI_HS_Q_MANUAL, day0 + h0 + 60);
                hpi_hs_record(HPI_HS_T_BP_DIA, 76 + rnd(5), QS | HPI_HS_Q_MANUAL, day0 + h0 + 60);
                fed += 3;
            }

            /* ---- continuous HRV: one 5-minute window, exactly as hpi_hs_hrv.c emits.
             *
             * The generator used to fake a single HRV_SDNN value per day and nothing
             * else -- no RMSSD, no coverage. That left the ENTIRE P3 path untested by
             * synthetic data: `stress_hrv` needs >=20 RMSSD windows to build a baseline
             * and there were zero, so the score could never go valid and the app's
             * "building your baseline" state had nothing to come out of.
             *
             * Faithfulness rules, both of which matter:
             *
             *  1. COVERAGE IS DERIVED, NOT INVENTED. It is the still-and-on-skin
             *     fraction of the window -- which is what it physically is. Motion and
             *     the charger punch holes in it for free.
             *  2. WE ONLY EMIT WHAT THE REAL PIPELINE WOULD. hrv_emit() discards a
             *     window under HRV_MIN_COVERAGE (50%) before recording it, so a
             *     sub-floor sample can never appear on a real device. Fabricating one
             *     here would make the fixture lie about the firmware -- an app team
             *     testing their coverage gate against it would be testing a case that
             *     does not exist. Their gate is still correct (our floor is a
             *     compile-time constant and can move); it just will not fire on this
             *     data. */
            for (int ws = h0; ws + HRV_WIN_S <= h1; ws += HRV_WIN_S) {
                int good = 0;

                for (int t = ws; t < ws + HRV_WIN_S; t += 5) {
                    if (!off_skin(t) && !exercising(t)) {
                        good += 5;   /* still + on-skin: beats are usable */
                    }
                }

                int32_t cov = (int32_t)(good * 100 / HRV_WIN_S);

                /* Beat loss, on top of the structural gaps above.
                 *
                 * The structural term ALONE is not enough, and the first version of this
                 * was wrong because of it: every boundary in this model (bouts at 07:30
                 * / 08:00, charger at 20:00 / 21:30) happens to fall on a 5-minute
                 * mark, so no window is ever PARTIALLY still -- coverage came out a flat
                 * 98% for every single window, and the app's coverage gate had nothing
                 * to act on.
                 *
                 * It was also unrealistic. Awake coverage is genuinely poor: the wearer
                 * fidgets, types, walks to the kitchen, and the confidence gate throws
                 * those beats away. Asleep, the wrist is still for minutes at a time and
                 * coverage is excellent. That contrast is the real shape of this signal,
                 * and it is why a nightly HRV reading is the one people trust. */
                int32_t quality = asleep(mid_of(ws)) ? (90 + rnd(8))    /* 82..98 */
                                                     : (68 + rnd(24));  /* 44..92 */
                cov = MIN(cov, quality);

                if (cov < 50) {
                    /* The real pipeline DISCARDS this before recording (HRV_MIN_COVERAGE),
                     * so it must never reach the store here either -- a fixture that emits
                     * samples the firmware cannot produce is a fixture that lies. Note
                     * this drops a good fraction of awake windows, which is correct: a
                     * restless window's RMSSD is an artefact, and an artefact still plots
                     * as a perfectly plausible line. */
                    continue;
                }

                int mid = mid_of(ws);
                int32_t rmssd = synth_rmssd_x10(mid, d, days);
                /* SDNN runs above RMSSD in a short window (it carries the slower
                 * respiratory variation that successive differences cancel out). */
                int32_t sdnn  = (rmssd * 12) / 10 + rnd(40);
                /* Mean R-R must AGREE with the HR we already published for this window,
                 * or the two series contradict each other on the app's own charts. */
                int32_t hr = synth_hr(mid);
                int32_t mean_rr = 60000 / (hr > 0 ? hr : 60);

                /* Exactly the quality hrv_emit() sets -- still, on-skin, high-confidence
                 * by construction -- plus the SYNTHETIC brand. */
                uint8_t qh = QS | HPI_HS_Q_ON_SKIN | HPI_HS_Q_LOW_MOTION |
                             HPI_HS_Q_HIGH_CONF;
                int64_t ts = day0 + ws + HRV_WIN_S;   /* ts = window END */

                hpi_hs_record(HPI_HS_T_HRV_RMSSD,    rmssd,   qh, ts);
                hpi_hs_record(HPI_HS_T_HRV_SDNN,     sdnn,    qh, ts);
                hpi_hs_record(HPI_HS_T_HRV_MEAN_RR,  mean_rr, qh, ts);
                hpi_hs_record(HPI_HS_T_HRV_COVERAGE, cov,     qh, ts);
                fed += 4;
                hrv_wins++;
            }

            /* Close the hour's open epochs, then DRAIN THE RING before the next hour
             * refills it. Without this the ring wraps (see the comment above). */
            hpi_hs_epoch_flush_all(day0 + h1);
            hpi_hs_flush_now();
            /* Real sleep, not k_yield(). This runs ~100 s of work; a bare yield does
             * not let the log backend drain, and the watchdog-fed SMF threads need
             * air. Cheap insurance: 168 sleeps x 10 ms = 1.7 s over a whole week. */
            k_sleep(K_MSEC(10));
        }

        /* ---- the midnight reset ----
         * The counter drops to 0 at the day boundary. This is precisely the case the
         * aggregator must force-flush: without it the window emits only its last
         * value (0) and the day's FINAL total is lost.
         *
         * Only on a COMPLETE day. Today has not hit midnight yet, so resetting it
         * here would zero steps_today and make SUMMARY look broken. */
        if (day_end == DAY_S) {
            hpi_hs_epoch_steps(0, QS, day0 + DAY_S - 1);
            fed++;
        }
        hpi_hs_epoch_flush_all(day0 + day_end);
        hpi_hs_flush_now();

        LOG_INF("synth: day %u/%u done (%d fed, head seq %u)",
                d + 1, days, fed, hpi_hs_head_seq());
        k_sleep(K_MSEC(20));
    }

    LOG_WRN("synth: done - %d synthetic samples fed to the aggregator. The store "
            "holds far fewer RECORDS than that (that is P1 working).", fed);

    /* Say plainly whether the stress score can even become valid on this dataset.
     * Below the baseline threshold `stress_hrv_v` stays FALSE by design, and a tester
     * who does not know that will read a perfectly correct refusal as a broken score. */
    LOG_WRN("synth: %d HRV windows emitted (stress baseline needs >= %d). "
            "stress_hrv should now read VALID; today is generated as a high-strain day "
            "on purpose, so expect a score ELEVATED above the neutral 50 - a fixture "
            "whose right answer is exactly the midpoint cannot tell a working score "
            "from a stub that returns the midpoint.",
            hrv_wins, HPI_HS_STRESS_MIN_BASELINE_WINDOWS);
    return fed;
}

/* ---- command-driven generation -------------------------------------------
 *
 * Triggered by the HPI_HS `SYNTH` MCUmgr command (id 6), not at boot.
 *
 * Boot-triggering was the old design and it was a liability: generation is a long,
 * heavy job, and if it ever faults partway (it did -- a stack overflow presenting as
 * a cold reboot) the device comes up, regenerates, and faults again. That is a boot
 * loop, and it needed a marker file purely to defend against itself. A command has
 * no such failure mode: nothing re-runs unless someone asks.
 *
 * It also removes the RUN_ID / marker / WIPE_FIRST machinery entirely -- re-running
 * is just "send the command again". */

static K_SEM_DEFINE(s_synth_req, 0, 1);
static uint32_t s_req_days;
static bool     s_req_wipe;
static atomic_t s_running = ATOMIC_INIT(0);

int hpi_hs_synth_request(uint32_t days, bool wipe)
{
    if (days == 0 || days > 30) {
        return -EINVAL;
    }
    if (!atomic_cas(&s_running, 0, 1)) {
        return -EBUSY;   /* one at a time */
    }
    s_req_days = days;
    s_req_wipe = wipe;
    k_sem_give(&s_synth_req);
    return 0;
}

static void synth_thread(void)
{
    for (;;) {
        k_sem_take(&s_synth_req, K_FOREVER);

        uint32_t days = s_req_days;
        bool wipe = s_req_wipe;

        LOG_WRN("synth: request accepted - %u day(s), wipe=%d", days, wipe);
        if (wipe) {
            hpi_hs_test_wipe();
        }
        hpi_hs_synth_generate(days, 0);

        /* Prove the point of P1: the exercise peak must survive in HR_MAX even though
         * the epoch MEAN blurs it away. This is the entire argument for emitting
         * min/max (8x reduction) rather than the mean alone (12x). */
        int64_t now = hw_get_sys_time_ts();
        struct hpi_hs_stats mean_st, max_st, min_st;

        hpi_hs_stats(HPI_HS_T_HR,     now - 86400, now, 0, &mean_st);
        hpi_hs_stats(HPI_HS_T_HR_MAX, now - 86400, now, 0, &max_st);
        hpi_hs_stats(HPI_HS_T_HR_MIN, now - 86400, now, 0, &min_st);

        LOG_WRN("VERIFY head=%u oldest=%u", hpi_hs_head_seq(), hpi_hs_oldest_seq());
        LOG_WRN("VERIFY HR mean-series : n=%u min=%d max=%d  <- peak is BLURRED here",
                mean_st.count, mean_st.min, mean_st.max);
        LOG_WRN("VERIFY HR_MAX series  : n=%u max=%d  <- the REAL exercise peak",
                max_st.count, max_st.max);
        LOG_WRN("VERIFY HR_MIN series  : n=%u min=%d  <- the REAL sleeping trough",
                min_st.count, min_st.min);

        struct hpi_hs_summary sum;
        hpi_hs_summary(&sum);
        LOG_WRN("VERIFY SUMMARY resting=%d min=%d avg=%d max=%d | tempdev=%d(v%d) steps=%u",
                sum.hr_resting, sum.hr_min, sum.hr_avg, sum.hr_max,
                sum.temp_dev_x100, sum.temp_dev_valid, sum.steps_today);

        atomic_set(&s_running, 0);
    }
}

/* Own thread, own stack, LOW priority. It must not run on a system thread: on the
 * store thread's 2 KB stack it overflowed, faulted into the cold-reboot handler, and
 * presented as a boot loop rather than a crash. */
K_THREAD_DEFINE(hpi_hs_synth_tid, 8192, synth_thread, NULL, NULL, NULL, 12, 0, 0);
