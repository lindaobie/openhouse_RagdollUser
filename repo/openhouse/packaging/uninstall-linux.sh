#!/usr/bin/env bash
# Puts SteamVR's own lighthouse driver back. Your room and settings in ~/.config/lh are kept.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/.steamvr-dir.sh"
# The update check goes first, so it's removed even when there's no driver backup.
check_registration unregister
remove_monado_check
rm -rf "$check_dir"
remove_schema
[[ -f "$backup" ]] || { echo "SteamVR's driver backup isn't there ($backup). Nothing to undo, or verify SteamVR's files in Steam." >&2; exit 1; }
mv "$backup" "$target"
rm -rf "$dir/lh-libs"
echo "SteamVR's own driver is back. Your room and settings in ~/.config/lh are kept."
