#!/bin/bash
# Source the nRF Connect SDK / Zephyr environment and run a west build.
# Usage:
#   ./scripts/env_build.sh              # incremental build
#   ./scripts/env_build.sh --pristine   # clean build
#
# Superseded by build.sh, which resolves the toolchain from the workspace SDK
# version instead of the hardcoded id below. Kept as a fallback.

NCS_BASE="/opt/nordic/ncs"
NCS_TOOLCHAIN_ID="5c0d382932"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "${SCRIPT_DIR}")"
WORKSPACE_DIR="$(dirname "${REPO_DIR}")"

# Use the workspace's own Zephyr (matches west.yml manifest)
export ZEPHYR_BASE="${WORKSPACE_DIR}/zephyr"
export PATH="${NCS_BASE}/toolchains/${NCS_TOOLCHAIN_ID}/bin:${PATH}"

# Source the Zephyr environment (sets CMake package paths etc.)
source "${ZEPHYR_BASE}/zephyr-env.sh"

PRISTINE_FLAG=""
if [ "$1" = "--pristine" ]; then
    PRISTINE_FLAG="--pristine"
fi

cd "${WORKSPACE_DIR}" && \
west build healthypi-move-fw/app ${PRISTINE_FLAG} \
    --board healthypi_move/nrf5340/cpuapp \
    -- -DBOARD_ROOT="${WORKSPACE_DIR}/healthypi-move-fw" \
       -DNCS_TOOLCHAIN_VERSION=NONE
