#!/usr/bin/env bash
#
# Clean (pristine) build of the HealthyPi Move application firmware.
#
# Thin wrapper over build.sh so there is a single source of truth for toolchain
# activation, SDK-version parsing, and — importantly — the ABSOLUTE -DBOARD_ROOT.
# The previous standalone `west build ... -DBOARD_ROOT=.` passed a relative ".",
# which resolves against the app source dir (no boards/ there) and triggers a
# spurious "BOARD_ROOT element without a 'boards' subdirectory" warning.
#
# Extra args are forwarded to build.sh / west build.
exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/build.sh" --pristine "$@"
