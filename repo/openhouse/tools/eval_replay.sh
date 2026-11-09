#!/bin/sh
# Replays a capture in parallel windows and scores tracking quality per device.
# usage: tools/eval_replay.sh CAPTURE.lhraw OUTDIR [window seconds] [lead-in seconds] [extra lhtool args...]
set -e
cap=$1; out=$2; win=${3:-50}; lead=${4:-4}; shift 4 2>/dev/null || shift $#
mkdir -p "$out"; rm -f "$out"/w*.jsonl
root=$(dirname "$0")/..
len=${LEN:-620}  # capture seconds; windows past the end finish at once
i=0; t=0
while [ "$(echo "$t < $len" | bc)" = 1 ]; do
  from=$(echo "$t - $lead" | bc); [ "$(echo "$from < 0" | bc)" = 1 ] && from=0
  name="$out/w$(printf %03d $i)-$t"
  "$root"/build-linux/lhtool session "$cap" --rate 1 --from "$from" --to "$(echo "$t + $win" | bc)" --trace "$name.jsonl" "$@" > "$name.events" 2>&1 &
  i=$((i + 1)); t=$(echo "$t + $win" | bc)
done
wait
python3 -I "$root"/tools/eval_replay.py "$out" "$lead"
