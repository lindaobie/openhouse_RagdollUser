#!/usr/bin/env bash
# Cross-build on Linux. Does not install or change the running Linux driver.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
# Dedicated checkout; deliberately not $VCPKG_ROOT (a system vcpkg checkout may lack
# the pinned baseline). Override with LH_VCPKG_ROOT.
root=${LH_VCPKG_ROOT:-${XDG_CACHE_HOME:-$HOME/.cache}/openhouse/vcpkg}
command -v x86_64-w64-mingw32-g++ >/dev/null || { echo 'Install MinGW-w64 first' >&2; exit 1; }
if [[ ! -f "$root/scripts/buildsystems/vcpkg.cmake" ]]; then
  git clone https://github.com/microsoft/vcpkg.git "$root"
fi
# Use this checkout's own vcpkg: a system vcpkg resolves the pinned baseline from
# its own registry checkout instead.
[[ -x "$root/vcpkg" ]] || "$root/bootstrap-vcpkg.sh" -disableMetrics
vcpkg="$root/vcpkg"
export VCPKG_ROOT="$root" VCPKG_MAX_CONCURRENCY="${VCPKG_MAX_CONCURRENCY:-16}"
triplet=x64-mingw-static-release
"$vcpkg" install --x-manifest-root="$repo" --x-install-root="$root/installed" \
  --overlay-triplets="$repo/cmake/triplets" --triplet=$triplet --x-feature=tests --x-feature=viewer
cmake -S "$repo" -B "$repo/build-win64" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$root/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$repo/cmake/mingw-w64.cmake" \
  -DVCPKG_TARGET_TRIPLET=$triplet -DVCPKG_OVERLAY_TRIPLETS="$repo/cmake/triplets" -DCMAKE_BUILD_TYPE=Release -DVCPKG_MANIFEST_MODE=OFF -DVCPKG_APPLOCAL_DEPS=OFF \
  -DOPENVR_INCLUDE_DIR="${OPENVR_INCLUDE_DIR:-/usr/include/openvr}" \
  -DLH_BUILD_TESTS=ON -DLH_BUILD_VIEWER=ON -DLH_BUILD_TOOLS=ON
cmake --build "$repo/build-win64" -j"${VCPKG_MAX_CONCURRENCY}"
python3 "$repo/scripts/package-windows.py" "$repo/build-win64" "$root/installed/$triplet"
