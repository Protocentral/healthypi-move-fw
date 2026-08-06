# HealthyPi Move — Health Store API (`HPI_HS`)

> **Status: live contract (schema v1 / group v3).** This is the **only** supported
> device↔app history and bulk-record sync path. The legacy BLE framed Command
> Service (`LOG_*` / `RECORDING_*` file pull, SOF `0x0A 0xFA`) is **removed** from
> firmware. Companion headers: `app/src/health/hpi_hs_types.h` (schema),
> `hpi_hs_sync.h` (group ids). Implementation: `hpi_hs_mgmt.c`. Status + history:
> `docs/ARCHITECTURE_REWRITE_PLAN.md` §0; advanced metrics: `docs/H6_ADVANCED_METRICS.md`.
> High-level summary also in `README.md`.

This is the contract the **HealthyPi Move app**, the **reference Python client**,
and any **third-party / research client** speak. It is intentionally
self-describing so a client needs no hard-coded schema.

## 1. Transport

The API is a **custom MCUmgr (SMP) management group**, id **`0x1000`**. Because
it rides the standard SMP stack it is **transport-agnostic**:

- **BLE** — SMP characteristic (`smp_bt`), available now.
- **USB-CDC / UART** — SMP serial (`smp`), same commands, when the USB stack is
  re-enabled. No protocol change; only the client's transport differs.

Requests/responses are **CBOR** maps (as in all MCUmgr groups). Any MCUmgr client
library works: the app uses its Flutter SMP/MCUmgr plugin; the reference client
uses Python `smpclient`.

## 2. Versioning

- `HPI_HS_SCHEMA_VERSION` (currently **1**) — meaning/unit/scale of sample types
  and the wire sample layout. Bumps only on a **breaking** change.
- `HPI_HS_GROUP_VERSION` (currently **3**) — the command-set shape.

Both are returned by `HELLO`. **Adding a new metric type id is not breaking** —
old clients skip unknown ids. Renumbering/reusing an id or changing a type's
unit/scale **is** breaking and bumps `HPI_HS_SCHEMA_VERSION`.

## 3. Sample schema (wire)

`SYNC` returns samples as a CBOR **byte string** of back-to-back **packed,
little-endian** 18-byte records (raw for compactness over BLE):

| Field | Type | Bytes | Notes |
|---|---|---|---|
| `seq` | `uint32` | 4 | monotonic per-device sequence; the **sync cursor** |
| `ts_utc` | `int64` | 8 | seconds since Unix epoch (UTC) |
| `type` | `uint8` | 1 | metric type id (§4) |
| `quality`| `uint8` | 1 | quality/context bitmask (§5) |
| `value` | `int32` | 4 | fixed-point; **real = value / scale** (per type) |

Python unpack: `struct.unpack('<IqBBi', rec)`. One value per sample; multi-field
metrics (BP sys/dia, EDA scl/scr) are **separate samples sharing `ts_utc`**.

## 4. Type registry

`scale` = divisor to real units (`real = value/scale`). `class`: D=discrete
(avg/min/max), C=cumulative (sum), E=event (sparse). `HK`/`HC` = the Apple
HealthKit / Android Health Connect type a bridge maps to. The device serves this
table verbatim via `TYPES` (do not hard-code it; read it).

| id | key | unit | scale | class | derived | HK | HC |
|----|-----|------|-------|-------|---------|----|----|
| 0x01 | `hr` | bpm | 1 | D | | `heartRate` | `HeartRateRecord` |
| 0x02 | `resting_hr` | bpm | 1 | D | ✓ | `restingHeartRate` | `RestingHeartRateRecord` |
| 0x03 | `ecg_hr` | bpm | 1 | E | | `heartRate` | `HeartRateRecord` |
| 0x10 | `spo2` | % | 1 | D | | `oxygenSaturation` | `OxygenSaturationRecord` |
| 0x20 | `skin_temp` | degC | 100 | D | | `bodyTemperature` | `SkinTemperatureRecord` |
| 0x21 | `skin_temp_dev` | degC | 100 | D | ✓ | `appleSleepingWristTemperature` | — |
| 0x30 | `bp_sys` | mmHg | 1 | E | | `bloodPressureSystolic` | `BloodPressureRecord` |
| 0x31 | `bp_dia` | mmHg | 1 | E | | `bloodPressureDiastolic` | `BloodPressureRecord` |
| 0x40 | `steps` | count | 1 | C | | `stepCount` | `StepsRecord` |
| 0x41 | `active_energy` | kcal | 1 | C | | `activeEnergyBurned` | `ActiveCaloriesBurnedRecord` |
| 0x50 | `hrv_sdnn` | ms | 10 | D | | `heartRateVariabilitySDNN` | — |
| 0x51 | `hrv_rmssd` | ms | 10 | D | | — | `HeartRateVariabilityRmssdRecord` |
| 0x52 | `hrv_lfhf` | ratio | 100 | D | ✓ | — | — |
| 0x60 | `eda_scl` | uS | 100 | D | | — | — |
| 0x61 | `eda_scr_rate` | /min | 1 | D | | — | — |
| 0x62 | `stress` | index | 1 | D | ✓ | — | — |

Notes for bridges: HealthKit `oxygenSaturation` is a 0..1 fraction → divide the
percent by 100. HealthKit has no RMSSD (use SDNN); Health Connect has no SDNN
(use RMSSD). EDA/stress have no standard type — export as custom/quantity.

## 5. Quality / context flags (`quality` byte)

| bit | flag | meaning |
|----|------|---------|
| 0 | `VALID` | timestamp valid (RTC synced) and value in range |
| 1 | `ON_SKIN` | sensor reports skin contact |
| 2 | `LOW_MOTION` | IMU below motion threshold |
| 3 | `HIGH_CONF` | sensor/algorithm confidence high |
| 4 | `DURING_SLEEP` | captured in a detected sleep window |
| 5 | `MANUAL` | user-initiated spot check |

Clients may re-filter (e.g. only `ON_SKIN|LOW_MOTION` for resting analysis). The
device already drops non-`VALID` samples before storing.

## 6. Commands

All are SMP **READ** except `ACK`, `SET_TZ` and the BPT-cal writes (**WRITE**). Group `0x1000`.

### `HELLO` (cmd 0, read)
`req {}` → `rsp { "schema":1, "group":3, "dev":"healthypi-move", "uid":"<hex>", "head":<uint>, "oldest":<uint>, "types":<uint> }`
Handshake: check `schema`/`group`, note `head` (newest seq).

- **`uid`** — per-unit id (hex of the SoC device id). **Key your sample store on
  this, not on `dev`.** `dev` is a fixed device-*class* string
  (`"healthypi-move"`), identical on every watch: two watches paired to one phone
  would collide on a `(device, seq)` primary key. `dev` is retained unchanged for
  backward compatibility. Empty string if the SoC returns no id.
- **`oldest`** — oldest seq still retrievable (segment retention drops the tail).
  Lets a client distinguish "store is empty" from "my cursor is stale" without
  probing: **`oldest > head` means the store holds nothing**. If your stored cursor
  is `< oldest - 1` you have missed samples that are gone; restart from
  `since = oldest - 1`.

### `TYPES` (cmd 1, read) — **paged**
`req { "from":<uint> }`
→ `rsp { "next":<uint>, "total":<uint>, "types":[ {"id","key","unit","scale","class","derived","hk","hc"}, ... ] }`
The registry is served in **pages** (currently 5 entries/call, to fit the 1024 B
SMP netbuf). Loop: start `from:0`, read the page, set `from:=next`, repeat until
`next == total`. A single call returns only the first page — a client that reads
`TYPES` once will see just 5 of the `total` (16) types. Fetch fully once, cache by `id`.

### `SYNC` (cmd 2, read) — the workhorse
`req { "since":<cursor>, "max":<uint> }`
→ `rsp { "recs":<bstr of N*18>, "n":N, "next":<cursor>, "more":<bool> }`
Loop: start at your stored cursor (0 = oldest retained), unpack `recs`, advance
to `next`, repeat while `more`. Idempotent on `seq`; resume after a dropped link
by re-requesting from `next`.

`more` means **"another page is worth fetching"**: it is `n > 0 && next < head`.
It is never `true` on an empty page, so looping on `more` alone terminates and
cannot spin on an unadvancing cursor. (Before FW 2.1.2 `more` was just
`next < head`, so it stayed `true` on an empty response — do not rely on the old
behaviour.)

Reads are served from the RAM ring when the cursor is recent and from the durable
segment files otherwise; the split is invisible to the client. A cold ring (any
reboot) is served from flash.

### `SYNTH` (cmd 6, write) — **TEST BUILDS ONLY**
`req { "days":<uint>, "wipe":<bool> }` → `rsp { "rc":0, "days":N, "wipe":B }`

Generates backdated **synthetic** data on-device, so trends, the 7-day skin-temp
baseline and sync-at-scale can be tested without wearing the watch for a week.

- **Returns immediately.** Generation runs on its own thread and takes ~100 s for a
  week (blocking the SMP thread would stall the BLE link and trip the watchdog).
  Poll `HELLO.head` to watch it grow. `rc = -EBUSY` if one is already running.
- `days` defaults to 7, `wipe` to true. `wipe` discards the existing durable log
  first so a re-run does not stack a second dataset on the first; **`seq` is never
  rewound** by it.
- **Every sample it writes carries `quality & (1<<6)` (`SYNTHETIC`).** Fabricated
  data shares the store with real data — the client **MUST** filter it out of
  anything user-facing. On a health device, test data must never render as a
  measurement.
- **The command does not exist in a release build** (`CONFIG_HPI_HS_SYNTH=n`).

### `SUMMARY` (cmd 3, read)
`req {}` → today-summary + baselines (resting HR, today HR min/avg/max, overnight
SpO₂, temp Δ vs baseline + nights, HRV vs baseline, HRV-stress `stress_hrv`/
`stress_hrv_v`, **H6 morning readiness `readiness` 0..100 / `readiness_v`**, steps,
energy, last stress) as a CBOR map mirroring `struct hpi_hs_summary`. Any `*_v`
flag false means "still forming — treat as no value", never zero. For at-a-glance
UI without replaying raw.

### `RECORDS` (cmd 4, read) — episodic raw-signal sessions (the Record tier)
Long-term / research captures (ECG, BioZ/GSR, wrist/finger PPG, HRV R-R, IMU),
each a self-describing session: header (`struct hpi_hs_record_hdr`) + a raw payload
fetched in chunks and CRC-verified. Three ops (as-built, H-REC):

**list** — `req { "op":"list", "from":<uint> }` (default op) → **paged**:
```
{ "next":<uint>, "total":<uint>,
  "recs":[ { "id":uint, "sig":uint, "fmt":uint, "ch":uint, "rate":uint,
             "ns":uint, "len":uint, "crc":uint, "flags":uint, "ts":int }, ... ] }
```
Loop `from:=next` until `next==total` (paged 6/call). `sig` = `enum hpi_hs_signal`,
`fmt` = `enum hpi_hs_sfmt`, `ns` = samples/channel, `len` = payload bytes, `crc` =
CRC-32 of the payload, `flags` = `HPI_HS_REC_F_*` (bit0 COMPLETE, bit1 PARTIAL),
`ts` = start UTC seconds.

**get** — `req { "op":"get", "id":<uint>, "off":<uint>, "len":<uint> }`
→ `{ "id":uint, "off":uint, "data":<bstr>, "eof":<bool> }`.
`off` is a **payload** offset (the header is not shipped); `len` is capped at 512 B
per call. Loop `off += len(data)` while `!eof`; CRC-32 the concatenated payload and
compare to the header `crc`.

**ack** — `req { "op":"ack", "id":<uint> }` → `{ "rc":0 }` (or negative errno:
`-ENOENT` no such record, `-EBUSY` still capturing). Device drops that record.

Interrupted sessions come back flagged `PARTIAL` (usable), not silently truncated.
This **replaces** the old whole-file MCUmgr-FS pull of `/lfs/{ecg,gsr,ppgw,ppgf,hrv}`
and the removed BLE `RECORDING_*` commands. Device capture for ECG / GSR / HRV R-R
is wired through `data_module` → `hpi_hs_rec_start` / `append` / `stop`; clients
should use `RECORDS` list/get/ack only (not raw FS group paths for health history).

### `ACK` (cmd 5, write) — retention
`req { "acked":<seq> }` → `rsp { "rc":0 }`
Tell the device the highest `seq` you've durably stored so it may drop retained
raw ≤ that (device also keeps a safety margin). Optional but recommended.

### `SET_TZ` (cmd 7, write) — UTC offset for the on-watch clock
`req { "off":<int seconds east of UTC> }` → `rsp { "rc":0 }`

The device keeps its RTC (and every stored/synced sample `ts_utc`) in **UTC** — the
MCUmgr `os datetime` set must therefore send **UTC**, not local time. This command
supplies the offset the watch applies **only** to its on-screen clock and local-day
boundaries; it never rewrites the RTC or shifts sample timestamps.

- `off` is whole seconds east of UTC, **DST-inclusive**: India `+19800`, US-Eastern
  in EST `-18000` / EDT `-14400`, Nepal `+20700`. Range `-43200..50400`.
- **Persisted** on the device (survives reboot). Send it once per connect right
  after the datetime set, and again whenever the phone's offset changes (DST /
  travel). Compute it with `DateTime.now().timeZoneOffset.inSeconds` (Dart), which
  already tracks DST.
- No RTC rewrite on DST — just re-send the new offset.

### `ERASE` (cmd 12, write, group v3) — delete all health data on the device

`req { "confirm":"ERASE" }` → `rsp { "rc":0, "head":<uint>, "oldest":<uint> }`

Erases **everything the device stores about the user's health**: the durable
sample log, every bulk record in the RECORDS tier, and any files left over from
pre-3.0 firmware. Settings, the user profile and BPT calibration are **kept** —
this is "delete my data", not a factory reset.

- **`confirm` is mandatory** and compared byte-for-byte against `"ERASE"`. A bare
  `{}`, a missing key or any other string is rejected with `-EINVAL` and nothing
  is touched. An irreversible command reachable by anything that can open an SMP
  session should not be one malformed CBOR map away from firing.
- **`seq` is not rewound.** It rounds **up** to the next segment boundary, exactly
  as a layout migration does, so a seq the client has already stored can never be
  reused by a later sample. Expect `oldest > head` afterwards — the documented
  "store is empty" answer (§`HELLO`).
- The response repeats the post-erase `head`/`oldest` so a client can reset its
  cursor without a second `HELLO`. A client that does nothing still recovers:
  `resumeCursor()` jumps a stale cursor forward on the next connect.
- `rc` is `-EBUSY` when a DFU is in progress or a capture is still running
  (stop the recording first — unlinking under an open writer would strand it).
- Erasing does **not** touch the copy already synced to the phone. That is a
  separate, client-side action.

The same erase is reachable **without a phone** from the watch itself:
*Settings › Erase data*, confirmed by a second tap on the row.

Firmware older than group v3 has no cmd 12 and answers `-EINVAL`; treat that as
"not supported" and point the user at the on-watch path.

### BPT calibration (cmds 8–11, group v2) — finger blood-pressure cal
Replaces the removed BLE Command Service verbs `0x60/0x61/0x62`. Calibration is a
fixed **3 points** (`idx` 0,1,2), each a reference cuff reading entered while the
finger is on the sensor. SMP is client→response only, so **live feedback is polled**
via `BPT_CAL_STATUS` (or a notify characteristic if one is re-added).

| Cmd | Op | Request | Response |
|---|---|---|---|
| `BPT_CAL_ENTER` (8) | write | `{}` | `{ "rc":0 }` — enter cal mode (idempotent) |
| `BPT_CAL_POINT` (9) | write | `{ "sys":u8, "dia":u8, "idx":u8 }` | `{ "rc":0 }` — start measuring point `idx` with reference `sys`/`dia` mmHg |
| `BPT_CAL_STATUS` (10) | read | `{}` | `{ "st":u8, "prog":u8, "idx":u8, "run":bool }` |
| `BPT_CAL_END` (11) | write | `{}` | `{ "rc":0 }` — leave cal mode |

- `rc`: `0` ok; **`-22` (`-EINVAL`)** bad/missing `idx`/`sys`/`dia` (`idx` must be 0–2,
  `sys`/`dia` 0–255); **`-16` (`-EBUSY`)** a point is already being measured.
- `BPT_CAL_STATUS`: `st` = MAX32664D calibration status, `prog` = 0–100, `idx` = current
  point, `run` = true while a point is in flight. Poll it ~5–10 Hz during a point.
- **`st` codes** (unchanged; `2` and `6` are terminal): `0` no signal · `1` good ·
  **`2` point complete** · `3`/`16`/`19` weak signal · `4` motion · **`6` calibration
  failed** · `23`/`24` no finger contact.
- Flow: `ENTER` → for each `idx` 0..2: `POINT{sys,dia,idx}` then poll `STATUS` until
  `st==2` (advance) or `st==6` (fail) → `END`. Cal vectors stay **device-local**
  (`/lfs/sys/bpt_cal_N`, 512 B each) — they are not sent over the wire.

## 7. Typical client flow

```
HELLO                       # check schema/group, read head
TYPES                       # cache registry by id (once)
loop:
  SYNC {since=cursor, max=256}
  for rec in recs: ingest(rec)         # dedup on seq
  cursor = next
  if not more: break
ACK {acked=cursor}          # allow device retention drop
SUMMARY                     # optional: refresh at-a-glance cards
```

The **phone is the system of record**: it accumulates samples and computes
long-term trends/baselines; the device retains only a rolling window.

## 8. Reference clients

- **Python** (`tools/hpi_hs_client/`, H4) — `smpclient` over BLE now (USB-CDC a
  one-line transport swap later). Dumps raw samples to CSV/Parquet/SQLite for full
  **data ownership**, no cloud required.
- **HealthyPi Move app** (Flutter) — uses its built-in SMP/MCUmgr plugin; the same
  commands, mapped into the app's store and optionally bridged to HealthKit /
  Health Connect via the `hk`/`hc` hints.

## 9. Open items before freeze
Baseline windows (temp ~5 nights, HRV 7-day vs 28-day) and the on-device retention
window are defaults, tunable without changing this contract. Sleep-window
detection source (IMU motion + wear vs a dedicated detector) affects which samples
carry `DURING_SLEEP` but not the wire format.
