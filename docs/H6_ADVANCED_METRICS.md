# HealthyPi Move — H6 Advanced Health Metrics

> Status: **live** (2026-07-18). The health-store verticals H0–H5, H-REC and
> H-CMD are complete (their record is in `ARCHITECTURE_REWRITE_PLAN.md` §0; the
> device↔app wire contract is `docs/HPI_HS_API.md`). This doc owns **H6** — the
> advanced *derived* metrics computed on top of the store. It replaces the H6
> section of the retired `HEALTH_DATA_ARCHITECTURE_PLAN.md`.

## What H6 is

H0–H5 built the store: typed sample ingest, correct query-time aggregation, a
durable log, MCUmgr sync, and the derived basics (resting HR, temp deviation,
HRV/SDNN/RMSSD, HRV-derived stress). H6 is the layer of **higher-order metrics a
user reads and acts on** — recovery, load, trends — each computed in
`hs_recompute_summary()` (store thread, ~5-min cadence) and surfaced through
`hpi_hs_summary()` / the `SUMMARY` MCUmgr command.

Design rules carried over from the store:
- **Scored against the user's own rolling baseline**, never absolute values —
  an HRV/RHR number is only meaningful relative to that person's normal.
- **Sleep-gated** where the metric is a recovery/overnight signal (the store's
  `DURING_SLEEP` window: on-skin + still + local night, gated at aggregation by
  timestamp — see the health-store quality work).
- **Sentinel, not zero**, until the baseline is real. A premature score is one
  the user would act on, so `*_valid = false` (and the fixed-point helpers return
  `-1`) until there's ~one night of data.
- **Fixed-point, FPU-free**, and the mapping split into a header-only pure
  function so it unit-tests without the filesystem/zbus/hw (like
  `hpi_hs_stress.h`).

## Implemented

### Readiness / recovery score  ✅ 2026-07-18
`app/src/health/hpi_hs_readiness.h` + `hs_recompute_summary()` in
`hpi_health_store.c`.

A morning recovery score, **0..100**, the Whoop-recovery / Oura-readiness /
Garmin-body-battery metric. Two nightly signals, each a deviation from the user's
own 7-day **sleep** baseline:

| Signal | Direction | Source |
|---|---|---|
| HRV (RMSSD) | **up** vs baseline → recovered | last night's sleep RMSSD vs 7-day sleep RMSSD baseline |
| Resting HR  | **down** vs baseline → recovered | last night's sleep HR vs 7-day sleep HR baseline |

**Mapping** (`hpi_hs_readiness()`, all fixed-point):
- HRV sub-score `= 100·rmssd_today/rmssd_base − 50`, clamped 0..100 (ratio 0.5→0, 1.0→50, 1.5→100).
- RHR sub-score `= 50 + (rhr_base − rhr_today)·5`, clamped 0..100 (5 pts/bpm; 10 bpm below → 100).
- `readiness = 0.6·HRV + 0.4·RHR` (HRV is the stronger recovery signal).
- Returns **−1** until both baselines have ≥ `HPI_HS_READINESS_MIN_*` (≈ one night) of data.

**Data flow.** Three sleep-gated accumulators run inside the single summary pass
(no extra scan, ~56 B): `rms_sleep` (today sleep RMSSD), `hr_sleep` (today sleep
HR = last night's RHR), `hrb_sleep` (7-day sleep HR = RHR baseline). The 7-day
sleep RMSSD baseline (`hrv_rmssd_base_x10`) already exists from the stress work.
Result lands in `hpi_hs_summary.readiness` / `.readiness_valid` and the `SUMMARY`
MCUmgr keys **`readiness` / `readiness_v`**.

**Tests.** `app/tests/hs_readiness` (8 cases): neutral=50, good=90, poor=10, the
60/40 weighting, clamps at 0/100, and the thin-baseline / invalid-input
sentinels. `west twister -T app/tests -p qemu_cortex_m3` → suite now **55**.

**On-device validation owed:** readiness needs real overnight wear to accrue the
sleep baselines; confirm the score is sane vs a reference wearable after a few
nights, and that it stays `readiness_v=false` until baselines form.

## Scoped / future

- **HR zones + daily cardio load (TRIMP-style)** — **blocked on user age.** Zones
  need max HR (≈ 220 − age); the user profile (`hpi_user_settings`) has height/
  weight/hand/units but **no age/DOB**. Add age to the profile first (settings +
  the settings screen + the app), then bucket today's HR into zones and integrate
  a load score. Until then, a resting-relative "active minutes" proxy is possible
  but weaker.
- **Stress-over-day** — the continuous HRV-derived stress already records
  `HPI_HS_T_STRESS` samples; a daily summary (mean + time-in-high) is a cheap
  `hs_recompute_summary` add (a couple of summary fields), and the per-window
  trend is already syncable via `SYNC`. Small, unblocked — a good next H6 slice.
- **Overnight summaries** — **largely already covered.** Overnight temp = the
  sleep-gated temp deviation (done); overnight HRV = the sleep baseline (done).
  Overnight **SpO₂ is not feasible** on this device — SpO₂ is a manual spot check,
  not continuous overnight sensing (that's a P7 sensing change).
- **Export** — raw samples + records already export via `SYNC` / `RECORDS`; a
  separate daily-summary export would duplicate `SUMMARY`. No firmware work owed.

## References
- Store phases + status: `docs/ARCHITECTURE_REWRITE_PLAN.md` §0 (health-data section).
- Wire contract: `docs/HPI_HS_API.md` (`SUMMARY` keys).
- Baseline/quality/sleep-gating: the health-store `hs_quality()` + `hs_ts_in_sleep_window()`.
- Pure-function precedent: `app/src/health/hpi_hs_stress.h`.
- Methodology: Whoop recovery, Oura readiness, Garmin HRV-status / body-battery
  (HRV + RHR vs personal baseline, measured during sleep).
