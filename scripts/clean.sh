#!/bin/bash
# Remove the build output so the next build starts fresh. Run from anywhere.
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(dirname "${SCRIPT_DIR}")"

rm -rf "${REPO_DIR}/app/build"
echo "Removed ${REPO_DIR}/app/build"
