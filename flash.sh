#!/usr/bin/env bash
#
# Flash the HealthyPi Move firmware.
#
#   ./flash.sh          # app core only (mcuboot + app), chip erase — the fast path
#   ./flash.sh --full   # all four domains, incl. the net core (b0n + ipc_radio)
#
# Use --full for: a fresh/recovered board, after `nrfutil device recover`, or
# whenever the net core actually changes (ipc_radio / b0n / a BLE controller
# config change). Otherwise the net core is untouched and reflashing it is pure
# wall-clock cost.
#
# ---------------------------------------------------------------------------
# Erase mode: --erase (chip erase) is deliberate and is what Nordic recommends
#
# NCS 3.4 doc (nrf/doc/nrf/app_dev/programming.rst, "Optional programming
# parameters") calls `west flash --erase` "the recommended" form, and offers the
# page-erase path — plain `west flash` — only as the alternative that "retains
# old data in other areas".
#
# Do NOT drop --erase to try to speed this up: without it the runner uses
# ERASE_RANGES_TOUCHED_BY_FIRMWARE ("Erasing address ranges touched by
# firmware"), which page-erases ~800 KB of app image and is SLOWER than one
# chip erase. Measured the wrong way round once already.
#
# What --erase costs you: it erases the INTERNAL flash of the cores being
# programmed, so `storage_partition` (settings NVS, internal @0xf0000) is wiped.
# The LittleFS volume — health store, records — lives on the EXTERNAL w25q01jv
# and is NOT touched: --erase only reaches external memory when the image itself
# refers to the XIP region (nrf_common.py: ext_mem_erase_opt), which this one
# does not.
#
# Why 4 domains at all: M3 removed Partition Manager, so there is no merged.hex
# and sysbuild flashes each domain separately, in the order given by
# app/build/domains.yaml:
#     mcuboot -> b0n -> ipc_radio -> app
#      (CPUAPP)  (CPUNET) (CPUNET)  (CPUAPP)
# Four images is CORRECT — it is the MCUboot + NSIB b0n + ipc_radio + app set
# this sysbuild.conf asks for, not a misconfiguration.
#
# The default below keeps the CPUAPP pair in their existing relative order
# (mcuboot then app) and simply omits the two CPUNET domains; --erase is
# per-core, so skipping them leaves the net core's b0n + ipc_radio in place.
# ---------------------------------------------------------------------------
set -euo pipefail

if [ "${1:-}" = "--full" ]; then
    west flash -d app/build --erase
else
    west flash -d app/build --erase --domain mcuboot --domain app
fi
