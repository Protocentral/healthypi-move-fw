#!/usr/bin/env bash
#
# Build the HealthyPi Move application firmware.
#
# Usage:
#   ./scripts/build.sh              # incremental build into app/build
#   ./scripts/build.sh --pristine   # clean build (west build flags pass through)
#   ./scripts/build.sh -p           # same, short form
#   RELEASE=1 ./scripts/build.sh --pristine   # the shipping artifact
#
# What this handles:
# - BOARD_ROOT is passed as an ABSOLUTE path. A relative "." resolves against the
#   CMake build directory (app/build), so Zephyr would look for app/build/boards/...
#   and report the board "not found". Using the repo root fixes board lookup.
# - Selects the NCS toolchain for the workspace SDK version (nrf/VERSION via
#   toolchains.json; exact match, else the oldest newer one installed) and
#   activates its FULL environment from environment.json:
#   the toolchain's own Python (with pykwalify etc.), west, the Zephyr SDK
#   compilers, nrfutil home and git. This is the same env the nRF Connect /
#   nrfutil "toolchain-manager" applies.
# - Uses the workspace's own Zephyr (matches west.yml), not the NCS install copy.
#
# Overrides (env vars): NCS_BASE, NCS_TOOLCHAIN_ID, ZEPHYR_BASE.
set -euo pipefail

# scripts/ lives one level below the repo root, which in turn sits inside the
# west workspace: <workspace>/healthypi-move-fw/scripts/build.sh
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "${SCRIPT_DIR}")"
WORKSPACE_DIR="$(dirname "${REPO_DIR}")"
NCS_BASE="${NCS_BASE:-/opt/nordic/ncs}"

# Workspace SDK version, e.g. "3.4.0" -> "v3.4.0". Since NCS 3.4 the nrf/VERSION
# file is the Kconfig-style multi-line form (VERSION_MAJOR/MINOR/PATCHLEVEL); older
# trees stored a bare "x.y.z" string. Handle both.
NCS_VERSION="v$(python3 - "${WORKSPACE_DIR}/nrf/VERSION" <<'PY'
import re, sys
txt = open(sys.argv[1]).read()
kv = dict(re.findall(r'(\w+)\s*=\s*(\S+)', txt))
if 'VERSION_MAJOR' in kv:
    print("%s.%s.%s" % (kv['VERSION_MAJOR'], kv['VERSION_MINOR'], kv.get('PATCHLEVEL', '0')))
else:
    print(txt.strip())
PY
)"

# Resolve the toolchain bundle id for this SDK version (unless explicitly given).
# Prefers an exact match; otherwise takes the oldest installed toolchain that is
# newer than the SDK version (e.g. SDK v3.4.0 builds with the v3.4.1 toolchain).
# Only bundles actually present on disk are considered.
TC_ID="${NCS_TOOLCHAIN_ID:-}"
if [ -z "${TC_ID}" ]; then
    TC_ID="$(python3 - "${NCS_BASE}/toolchains" "${NCS_VERSION}" <<'PY'
import json, os, re, sys
tc_dir, want = sys.argv[1], sys.argv[2]

def parse(v):
    m = re.match(r'v?(\d+)\.(\d+)\.(\d+)', v)
    return tuple(int(x) for x in m.groups()) if m else None

want_v = parse(want)
with open(os.path.join(tc_dir, "toolchains.json")) as f:
    data = json.load(f)
candidates = []
for group in data:
    for tc in group.get("toolchains", []):
        bundle = tc["identifier"]["bundle_id"]
        if not os.path.isfile(os.path.join(tc_dir, bundle, "environment.json")):
            continue
        for v in tc.get("ncs_versions", []):
            if v == want:
                print(bundle)
                sys.exit(0)
            pv = parse(v)
            if pv and want_v and pv >= want_v:
                candidates.append((pv, bundle))
if candidates:
    print(min(candidates)[1])
    sys.exit(0)
sys.exit(1)
PY
)" || {
        echo "build.sh: no installed NCS toolchain for ${NCS_VERSION} or newer." >&2
        echo "  Install it (nRF Connect 'Install Toolchain' or nrfutil), or set NCS_TOOLCHAIN_ID." >&2
        exit 1
    }
fi

TC_ROOT="${NCS_BASE}/toolchains/${TC_ID}"
[ -f "${TC_ROOT}/environment.json" ] || {
    echo "build.sh: ${TC_ROOT}/environment.json not found" >&2; exit 1; }

echo "build.sh: NCS ${NCS_VERSION}, toolchain ${TC_ID}"

# Activate the toolchain's declared environment (PATH incl. its Python venv & the
# Zephyr SDK compilers, ZEPHYR_SDK_INSTALL_DIR, ZEPHYR_TOOLCHAIN_VARIANT, etc.).
eval "$(python3 - "${TC_ROOT}" <<'PY'
import json, os, shlex, sys
root = sys.argv[1]
with open(os.path.join(root, "environment.json")) as f:
    spec = json.load(f)
out = []
for var in spec.get("env_vars", []):
    key = var["key"]
    if var["type"] == "string":
        val = var["value"]
    else:  # relative_paths -> absolute, ":"-joined
        val = ":".join(os.path.join(root, p) for p in var["values"])
        if var.get("existing_value_treatment") == "prepend_to":
            cur = os.environ.get(key, "")
            if cur:
                val = val + ":" + cur
    out.append("export %s=%s" % (key, shlex.quote(val)))
print("\n".join(out))
PY
)"

# Use the workspace's own Zephyr (matches the west.yml manifest).
export ZEPHYR_BASE="${ZEPHYR_BASE:-${WORKSPACE_DIR}/zephyr}"
# shellcheck disable=SC1091
source "${ZEPHYR_BASE}/zephyr-env.sh"

command -v west >/dev/null 2>&1 || { echo "build.sh: west not on PATH after toolchain activation" >&2; exit 1; }

# Release artifact:  RELEASE=1 ./build.sh --pristine
# Layers app/overlay-release.conf on top of prj.conf for the app-core image only
# (console/logging off, CONFIG_ASSERT=n — the overlay explains why the assert
# setting is a field-DFU requirement). "app" is the sysbuild image name for this
# application, hence the app_-prefixed variable.
EXTRA_CMAKE_ARGS=()
WANT_RELEASE=0
if [ "${RELEASE:-0}" = "1" ]; then
    WANT_RELEASE=1
    echo "build.sh: RELEASE build - applying app/overlay-release.conf"
    EXTRA_CMAKE_ARGS+=("-Dapp_EXTRA_CONF_FILE=${REPO_DIR}/app/overlay-release.conf")
fi

# -Dapp_EXTRA_CONF_FILE lands in the CMake cache and STAYS there: without this,
# one RELEASE=1 build turns every later incremental build into a release build
# too — no asserts, no console — with nothing on screen to say so. Switching
# between the two therefore has to start from a clean build dir.
CACHE_FILE="${REPO_DIR}/app/build/CMakeCache.txt"
CACHED_RELEASE=0
if [ -f "${CACHE_FILE}" ] && grep -q "app_EXTRA_CONF_FILE.*overlay-release\.conf" "${CACHE_FILE}"; then
    CACHED_RELEASE=1
fi
if [ "${CACHED_RELEASE}" != "${WANT_RELEASE}" ]; then
    already_pristine=0
    for arg in "$@"; do
        case "${arg}" in -p|--pristine|--pristine=*) already_pristine=1 ;; esac
    done
    if [ "${already_pristine}" -eq 0 ]; then
        if [ "${WANT_RELEASE}" -eq 1 ]; then
            echo "build.sh: switching dev -> RELEASE; forcing --pristine"
        else
            echo "build.sh: existing build dir is a RELEASE build; forcing --pristine for a dev build"
        fi
        set -- --pristine "$@"
    fi
fi

west build \
    --build-dir "${REPO_DIR}/app/build" \
    "${REPO_DIR}/app" \
    --board healthypi_move/nrf5340/cpuapp \
    "$@" \
    -- -DBOARD_ROOT="${REPO_DIR}" \
       -DNCS_TOOLCHAIN_VERSION=NONE \
       ${EXTRA_CMAKE_ARGS[@]+"${EXTRA_CMAKE_ARGS[@]}"}
