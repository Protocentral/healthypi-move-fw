#!/usr/bin/env bash
#
# Flash a RELEASED firmware pair from bin/ onto a watch over SWD, using nrfutil.
#
# This exists for upgrade testing: to prove an over-the-air update from an older
# release to the current build, you first have to get that older release onto a
# device, and the only way in is SWD (no fielded bootloader has serial recovery).
#
#   ./scripts/flash_bin.sh             # the only version in bin/, or list if ambiguous
#   ./scripts/flash_bin.sh v2.2.0      # a specific release
#   ./scripts/flash_bin.sh --list      # what is available
#   ./scripts/flash_bin.sh v2.2.0 --app-only
#
# bin/ holds the artifacts published by .github/workflows/build.yml:
#   healthypi_move_<ver>.hex          application core (MCUboot + app)
#   healthypi_move_cpunet_<ver>.hex   network core (b0n + ipc_radio)
#
# For the CURRENT working tree use ./scripts/flash.sh instead — it flashes
# app/build via west and knows about the QSPI configuration.
#
# --- What this does NOT touch -------------------------------------------------
#
# The external QSPI NOR is left alone: nrfutil programs the internal flash of the
# selected core, and neither hex reaches off-chip memory. That is deliberate —
# the LittleFS volume (health store, records, settings file) survives, which is
# what makes "settings and history survive an update" testable.
#
# The flip side: the MCUboot *secondary* slot is also on that QSPI part. If a
# previous DFU left an image staged there with its trailer magic set, the
# firmware you just flashed may swap it in on the next boot and quietly undo the
# downgrade. If the version after boot is not what you flashed, that is why —
# erase the external flash (e.g. ./scripts/flash.sh, which programs the QSPI
# config) and start again.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "${SCRIPT_DIR}")"
BIN_DIR="${REPO_DIR}/bin"

APP_ONLY=0
NET_ONLY=0
DO_RECOVER=1
ASSUME_YES=0
SERIAL=""
VERSION=""

usage() {
    sed -n '3,30p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    cat <<'EOF'

Options:
  --list          List the releases available in bin/ and exit
  --serial <SN>   Target a specific probe (default: the only one attached)
  --app-only      Program the application core only
  --net-only      Program the network core only
  --no-recover    Skip the recover step (faster; fails on a protected device)
  -y, --yes       Do not ask for confirmation before writing to the device
  -h, --help      This text
EOF
}

# Releases present in bin/, newest-looking last. Matches both the release and the
# dev-build naming the CI workflow produces.
list_versions() {
    local f v
    for f in "${BIN_DIR}"/healthypi_move_*.hex; do
        [ -e "$f" ] || continue
        v="$(basename "$f")"
        case "$v" in
            *_cpunet_*) continue ;;   # the net-core half of a pair
        esac
        v="${v#healthypi_move_}"
        v="${v%.hex}"
        echo "$v"
    done | sort -V
}

while [ $# -gt 0 ]; do
    case "$1" in
        --list)
            if [ -z "$(list_versions)" ]; then
                echo "flash_bin.sh: no firmware in ${BIN_DIR}" >&2; exit 1
            fi
            echo "Releases in bin/:"
            while read -r v; do
                net="${BIN_DIR}/healthypi_move_cpunet_${v}.hex"
                if [ -f "$net" ]; then echo "  $v  (app + net core)"; else echo "  $v  (app core only)"; fi
            done < <(list_versions)
            exit 0
            ;;
        --serial)     SERIAL="${2:-}"; shift 2 ;;
        --app-only)   APP_ONLY=1; shift ;;
        --net-only)   NET_ONLY=1; shift ;;
        --no-recover) DO_RECOVER=0; shift ;;
        -y|--yes)     ASSUME_YES=1; shift ;;
        -h|--help)    usage; exit 0 ;;
        -*)           echo "flash_bin.sh: unknown option $1" >&2; usage >&2; exit 1 ;;
        *)            VERSION="$1"; shift ;;
    esac
done

command -v nrfutil >/dev/null 2>&1 || {
    echo "flash_bin.sh: nrfutil not found on PATH" >&2
    echo "  install it from https://www.nordicsemi.com/Products/Development-tools/nrf-util" >&2
    exit 1
}

[ -d "${BIN_DIR}" ] || { echo "flash_bin.sh: ${BIN_DIR} does not exist" >&2; exit 1; }

# Resolve the version: explicit argument, or the only one present.
AVAILABLE="$(list_versions)"
[ -n "${AVAILABLE}" ] || { echo "flash_bin.sh: no firmware in ${BIN_DIR}" >&2; exit 1; }

if [ -z "${VERSION}" ]; then
    if [ "$(echo "${AVAILABLE}" | wc -l | tr -d ' ')" -eq 1 ]; then
        VERSION="${AVAILABLE}"
    else
        echo "flash_bin.sh: several releases in bin/ — name one:" >&2
        echo "${AVAILABLE}" | sed 's/^/  /' >&2
        exit 1
    fi
fi

# Accept "2.2.0" for "v2.2.0" and vice versa, so either habit works.
APP_HEX="${BIN_DIR}/healthypi_move_${VERSION}.hex"
if [ ! -f "${APP_HEX}" ] && [ -f "${BIN_DIR}/healthypi_move_v${VERSION}.hex" ]; then
    VERSION="v${VERSION}"
    APP_HEX="${BIN_DIR}/healthypi_move_${VERSION}.hex"
fi
NET_HEX="${BIN_DIR}/healthypi_move_cpunet_${VERSION}.hex"

[ -f "${APP_HEX}" ] || { echo "flash_bin.sh: no such release '${VERSION}' (try --list)" >&2; exit 1; }
if [ ! -f "${NET_HEX}" ] && [ "${APP_ONLY}" -eq 0 ]; then
    echo "flash_bin.sh: ${NET_HEX##*/} missing — programming the application core only." >&2
    APP_ONLY=1
fi

# Probe selection. With exactly one board attached nrfutil picks it itself, so
# only pass --serial-number when the user asked for a specific one.
DEV_ARGS=()
[ -n "${SERIAL}" ] && DEV_ARGS+=(--serial-number "${SERIAL}")

echo "flash_bin.sh: release ${VERSION}"
[ "${NET_ONLY}" -eq 0 ] && echo "  app core: ${APP_HEX##*/} ($(du -h "${APP_HEX}" | cut -f1))"
[ "${APP_ONLY}" -eq 0 ] && echo "  net core: ${NET_HEX##*/} ($(du -h "${NET_HEX}" | cut -f1))"
echo

# This overwrites whatever is on the watch, so say so and stop unless the caller
# has opted out of the prompt. Downgrading a device is easy to do by accident and
# annoying to undo (the only way back is another SWD flash).
if [ "${ASSUME_YES}" -eq 0 ]; then
    echo "This ERASES and reprograms the attached device."
    if [ -t 0 ]; then
        printf "Continue? [y/N] "
        read -r reply
        case "${reply}" in
            [yY]|[yY][eE][sS]) ;;
            *) echo "aborted"; exit 1 ;;
        esac
    else
        echo "flash_bin.sh: refusing to run non-interactively without --yes" >&2
        exit 1
    fi
    echo
fi

# Recover clears readback protection and wipes UICR on each core. Worth doing by
# default when moving between firmware generations: the secure-boot (b0n)
# provisioning lives in UICR/OTP, and a plain program does not clear it. Nordic's
# documented order for the nRF5340 is network core first, then application.
if [ "${DO_RECOVER}" -eq 1 ]; then
    if [ "${APP_ONLY}" -eq 0 ]; then
        echo "==> recover (network core)"
        nrfutil device recover --core Network "${DEV_ARGS[@]+"${DEV_ARGS[@]}"}"
    fi
    if [ "${NET_ONLY}" -eq 0 ]; then
        echo "==> recover (application core)"
        nrfutil device recover --core Application "${DEV_ARGS[@]+"${DEV_ARGS[@]}"}"
    fi
fi

# Program the network core before the application core, mirroring the flash order
# sysbuild itself uses (domains.yaml: mcuboot, b0n, ipc_radio, app).
if [ "${APP_ONLY}" -eq 0 ]; then
    echo "==> programming network core"
    nrfutil device program --firmware "${NET_HEX}" --core Network \
        --options chip_erase_mode=ERASE_ALL "${DEV_ARGS[@]+"${DEV_ARGS[@]}"}"
fi

if [ "${NET_ONLY}" -eq 0 ]; then
    echo "==> programming application core"
    nrfutil device program --firmware "${APP_HEX}" --core Application \
        --options chip_erase_mode=ERASE_ALL "${DEV_ARGS[@]+"${DEV_ARGS[@]}"}"
fi

echo "==> reset (pin)"
nrfutil device reset --reset-kind RESET_PIN "${DEV_ARGS[@]+"${DEV_ARGS[@]}"}"

# Follow with a soft reset over CTRL-AP. The pin reset restarts the part, but the
# debugger can still be holding the core after a program+reset sequence, and both
# cores need to come out together for the net core to hand over a working BLE
# controller. Not fatal if the probe or device does not implement it (the help
# for RESET_SOFT says it depends on the CTRL-AP implementation) — the pin reset
# above has already done the essential work.
echo "==> reset (soft)"
if ! nrfutil device reset --reset-kind RESET_SOFT "${DEV_ARGS[@]+"${DEV_ARGS[@]}"}"; then
    echo "flash_bin.sh: soft reset not supported here — continuing" >&2
fi

cat <<EOF

Done — ${VERSION} is on the watch.

Check the boot banner reports the version you expect before testing an update:
the application prints its own "Booting HealthyPi Move v<x>" line, and that is
the version DIS reports to the phone, which is what the app's update gating reads.

If it boots something else, an image staged in the external-flash secondary slot
was swapped in on first boot (see the note at the top of this script).
EOF
