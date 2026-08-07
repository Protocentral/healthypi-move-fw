# Changelog

All notable changes to the HealthyPi Move firmware are recorded here. Format is
based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the project
follows [semantic versioning](https://semver.org/spec/v2.0.0.html) as expressed
in [app/VERSION](app/VERSION).

Two contracts have their own version numbers and are called out per release when
they move: `HPI_HS_SCHEMA_VERSION` (sample meaning/layout) and
`HPI_HS_GROUP_VERSION` (the MCUmgr command-set shape). See
[docs/HPI_HS_API.md](docs/HPI_HS_API.md).

Releases up to and including **v3.0.2** predate this file; see the
[GitHub releases](https://github.com/Protocentral/healthypi-move-fw/releases)
and the commit history for those.

## [3.1.0] — 2026-08-07

`HPI_HS_GROUP_VERSION` 2 → **3**. Schema version unchanged (1).

### Added

- **Erase all health data** — `HPI_HS_CMD_ERASE` (cmd 12, write) over MCUmgr,
  and a **Settings › Erase data** row on the watch itself. Erases the durable
  sample log, every bulk record in the RECORDS tier, and any pre-3.0 leftovers.
  Settings, the user profile and BPT calibration are kept — this is "delete my
  data", not a factory reset. Previously there was no way to delete health data
  at all: the only wipe was `hpi_hs_test_wipe()`, absent from release builds.
  - `confirm` is mandatory and compared byte-for-byte against `"ERASE"`; a bare
    `{}` or any other string is rejected with `-EINVAL` and nothing is touched.
  - `seq` and record ids are **not rewound** — they round up to the next segment
    boundary, so an id the phone has already stored can never be reused.
  - The response carries the post-erase `head`/`oldest`, so a client can reset
    its cursor without a second `HELLO`.
- **One-shot purge of pre-3.0 storage on upgrade** (`app/src/hpi_storage_migrate.c`).
  A watch upgraded from pre-3.0 firmware kept its whole legacy tree
  (`/lfs/trhr`, `/lfs/trspo2`, `/lfs/trtemp`, `/lfs/trsteps`, `/lfs/trbpt`,
  `/lfs/ecg`, `/lfs/gsr`, `/lfs/log`) indefinitely on the 112 MB LittleFS
  volume — `fs_module_init()` only builds the directory structure when
  `/lfs/sys` is *missing*, which on an upgraded unit it never is.
  - Legacy data is **deleted, not converted**. Pre-3.0 firmware had no `SET_TZ`
    and stored wall-clock time, so its timestamps carry an unknown UTC offset
    while the health store's wire contract is UTC; converting would mean
    publishing knowingly wrong timestamps.
  - Stamped by `HPI_STORAGE_REV` in `/lfs/sys/storage_rev`, so it is idempotent
    and additive: each future fixup appends a new revision number and its own
    `if (rev < N)` block rather than extending an existing step. A stamp that
    cannot be read floors the revision to 0 so every step re-runs.
  - Runs on `hpi_sys_thread`, off the boot critical path, and re-checks
    `hpi_dfu_is_active()` between directories — the QSPI die also holds the DFU
    secondary slots, and the first boot after an OTA is exactly when this runs.
    A DFU mid-purge returns `-EAGAIN` with the revision unstamped, and it
    retries on the next boot.
- **`CONFIG_HPI_STORAGE_LEGACY_SYNTH` (test builds only)** — builds a synthetic
  pre-3.0 tree so the purge can be exercised on a bench unit that was flashed
  with 3.x and never had one. Triggered by `LEGACY_SYNTH` (cmd 13), which
  answers `-ENOTSUP` in a release image. Default 12 files per directory, above
  the walker's batch of 8, so the multi-pass loop is exercised; `/lfs/log`
  additionally gets an over-long name and a stray subdirectory, the two entries
  the walker has to cope with rather than choke on.

### Changed

- The erase runs on `hpi_sys_thread` rather than the caller's thread or the
  system workqueue. The Settings row runs on the display thread, which would
  trip its task watchdog; `HPI_HS_CMD_ERASE` runs on the SMP thread, whose stack
  is sized for CBOR rather than for the littlefs call chain behind a few hundred
  unlinks; and the system workqueue is cooperative and shared with BLE, sensor
  and settings work. `hpi_sys_thread` was already idle after init and already
  runs the boot purge. Its stack grew 2 KB → 3 KB.
- Erase submission is single-slot and its run state lives in the storage module,
  not the Settings screen — the screen is created and destroyed as the user
  navigates and an erase outlives it.
- The SMP `ERASE` handler stays synchronous from the client's point of view: it
  polls for up to 30 s and answers `-EINPROGRESS` if the erase is still running,
  **without** cancelling it.
- `hpi_hs_wipe_all()` replaces the `CONFIG_HPI_HS_SYNTH`-gated
  `hpi_hs_test_wipe()`, which is kept as a thin alias.
- App-core RAM 439568 B → 440600 B (97.79%); FLASH 780496 B in shipping shape.

### Fixed

- **Erase left the user's last values on flash.** The layout migration deletes
  only segments, so `/lfs/hs/lat` — last HR, SpO₂, skin temp, BP, HRV, EDA,
  stress, steps, energy — survived and was restored at boot and served to the
  watch faces and to `SUMMARY`. It is now unlinked and the RAM copy cleared.
  `HS_META` still survives: it carries the seq cursor, which must never rewind.
- **Records past the 24th were orphaned by an erase**, which still reported
  success: the delete walked `s_index`, capped at `HS_REC_MAX` and filled by an
  unsorted `readdir` at boot. It now sweeps the directory, sharing one stricter
  "is this a record file?" predicate with boot recovery (`rmeta` is excluded, or
  record ids rewind).
- **The purge loop could run forever.** A name ≥ `NAME_MAX_L` was silently
  truncated by `strncpy`, so `fs_unlink` returned `-ENOENT`, which was counted as
  success, and the next pass re-collected the same entry. Over-long names are now
  skipped rather than truncated (a truncated path names a *different* file),
  `-ENOENT` is no longer progress, and a pass that deletes nothing bails.
- **The storage revision was stamped even when the purge returned `-EAGAIN`**,
  recording "pre-3.0 tree is gone" over a tree still on flash, so no later boot
  would look again. It now stamps only on success.
- The **Erase row rendered no icon** — `make_row()` uses `matsym_26` and U+F083
  exists only in `matsym_24`; LVGL drops a missing glyph silently. Overridden for
  that row until 0xF083 is added to the `matsym_26` range and regenerated.
- **Leaving Settings mid-erase could start a second concurrent erase**, after
  which the row reported a stale result while the first was still running. The
  row now seeds its state from the worker.
- **Reclaimed-KB underflowed** a `uint64` to ~18 exabytes when the second
  `statvfs` failed.
- Dropped the impossible `lfs/hrv` legacy entry — Zephyr rejects relative paths
  at both `fs_mkdir` and `fs_opendir`, so the pre-3.0 typo never created that
  directory and listing it only produced a spurious `ERR`+`WRN` pair on every
  migrating unit.
- `hpi_hs_wipe_all()` called `hs_migrate_layout()` from above its definition and
  only ever compiled because release builds `#if`'d the whole block out.

### Known gaps

- The **display thread's live `m_disp_*` readings are not cleared by an erase**.
  `hpi_disp_restore_last_from_store()` is boot-only, so the faces are blank after
  a reboot, but clearing live values needs a UI-wide subject reset that has no
  existing API.
- The **DFU/upgrade validation matrix has not been run on hardware.** Building
  the test fixture is not the same as validating with it.
