#!/usr/bin/env bash
# Builds the lhbs base station firmware from scratch, runs its host test and packages it
# with the installers (scripts/package-firmware.py).
# Output: dist/openhouse-basestation-firmware/ and dist/openhouse-basestation-firmware.zip
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
fw="$repo/basestation_firmware"
command -v arm-none-eabi-gcc >/dev/null || { echo 'Install the ARM toolchain (arm-none-eabi-gcc) first' >&2; exit 1; }
command -v gcc >/dev/null || { echo 'Install gcc (for the host test) first' >&2; exit 1; }
make -s -C "$fw" clean
make -s -C "$fw" hosttest
python3 "$repo/scripts/package-firmware.py"
