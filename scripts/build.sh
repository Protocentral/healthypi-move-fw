#!/bin/bash
# Build HealthyPi Move firmware. sysbuild builds BOTH nRF5340 cores
# (cpuapp + cpunet) plus the MCUBoot bootloader in one pass.
# Self-contained: sources the nRF Connect SDK / Zephyr environment first.
# Run from anywhere. Extra args are passed to `west build`
# (e.g. `./scripts/build.sh --pristine`).
set -e

# --- nRF Connect SDK install (edit NCS_BASE to match your machine) ---
NCS_BASE="/opt/nordic/ncs"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "${SCRIPT_DIR}")"
WORKSPACE_DIR="$(dirname "${REPO_DIR}")"

# Always use the most recently installed toolchain (newest by mtime).
NCS_TOOLCHAIN_DIR="$(ls -dt "${NCS_BASE}"/toolchains/*/ 2>/dev/null | head -n 1)"
NCS_TOOLCHAIN_DIR="${NCS_TOOLCHAIN_DIR%/}"
if [ -z "${NCS_TOOLCHAIN_DIR}" ]; then
    echo "error: no nRF Connect SDK toolchain found under ${NCS_BASE}/toolchains" >&2
    exit 1
fi
echo "Using toolchain: ${NCS_TOOLCHAIN_DIR}"

# Use the workspace's own Zephyr (matches west.yml manifest) and that toolchain.
export ZEPHYR_BASE="${WORKSPACE_DIR}/zephyr"
export PATH="${NCS_TOOLCHAIN_DIR}/bin:${PATH}"
if [ -d "${NCS_TOOLCHAIN_DIR}/opt/zephyr-sdk" ]; then
    export ZEPHYR_SDK_INSTALL_DIR="${NCS_TOOLCHAIN_DIR}/opt/zephyr-sdk"
    export ZEPHYR_TOOLCHAIN_VARIANT="zephyr"
fi
source "${ZEPHYR_BASE}/zephyr-env.sh"

cd "${REPO_DIR}"
west build --build-dir app/build app \
    --board healthypi_move/nrf5340/cpuapp "$@" \
    -- -DBOARD_ROOT=. -DNCS_TOOLCHAIN_VERSION=NONE
