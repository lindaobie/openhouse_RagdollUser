# Generic recording tool

(To record while the driver runs inside SteamVR or Monado, see
[Recording from inside the host](#recording-from-inside-the-host) below.)

Close SteamVR, turn the controller on, and run:

```sh
./build/lhcapture
```

Move/occlude the controller however you want. **Ctrl-C finishes and saves.**
The printed output directory is the session to keep and give back for analysis.

Optional named directory or time limit:

```sh
./build/lhcapture captures/controller-session-01
./build/lhcapture captures/controller-session-02 --seconds 120
```

Existing directories are never overwritten. Without a directory argument, the tool
creates `captures/session-<UTC>-<pid>/`. With no duration, it records until Ctrl-C.
All accessible Lighthouse devices are recorded, including the controller and HMD.
The tool displays per-device IMU/light counts once a second. It does not run our
tracker or require following a predefined motion sequence.

You may optionally type a note and press Enter while recording. Notes are timestamped
using the same host clock as raw reports. They are not required.

## Saved files

- `input.lhraw`: raw USB/HID reports, endpoint configuration, timing, and range modes.
- `session.json`: start/stop times, endpoint details, per-device counts, file health,
  and completion status. Updated during recording.
- `markers.jsonl`: optional notes plus capture-ready/stop markers.
- `device-configs/`: readable copies of the configurations used, with serial mappings.
- `lighthousedb.json`: read-only snapshot of SteamVR's cached database, if available.

Keep the **whole directory**, not just pose output. Raw input is what we replay
through subsequent versions and the original-function oracle tests. These sessions
contain input data, not a synchronized stock-driver pose reference.

The recorder refuses to start while `vrserver` is running because the current USB
backend performs its own initialization/protocol negotiation. It never modifies
the cached device config or DB files. Recording or final flush failures cause a
nonzero exit; an incomplete session is retained rather than deleted. No IMU/light
input also produces a warning and nonzero exit, while preserving the raw file.

## Build / inspect

```sh
cmake --build build --target lhcapture -j2
./build/lhtool stats captures/controller-session-01/input.lhraw
```

`lhcapture` links only the USB transport and JSON library, not the tracking engine.
No live hardware recording was made as part of the tool's automated tests. Tests
cover help/argument rejection/no-overwrite behavior, raw-file round-trip, buffered
write failure detection, and existing capture replay.

## Recording from inside the host

`lhcapture` needs the devices to itself. To see what went wrong *in a game* (hands
flying, tracking churn, haptics, fingers), let the driver record while it runs:

```sh
LH_CAPTURE=$HOME/lh-captures/vrchat-01 LH_LOG_LEVEL=DEBUG <start Monado / WiVRn / SteamVR as usual>
```

The variable has to reach the process that loads `driver_lighthouse.so` (the Monado /
WiVRn server, or `vrserver`). For Steam, put it in the launch options or start Steam
from a shell that has it. An existing directory is never overwritten: the driver
uses `<dir>-2`, `-3`, ... Files:

- `input.lhraw`: every raw USB report, replayable with
  `lhviz --replay <dir>/input.lhraw --steam-db <dir>/lighthousedb.json` and `lhtool session`.
- `driver.jsonl`: what the host was given, on the session's host clock: each device's
  pose (up to 100 Hz, plus every change of valid/held/result/connected), the host
  frame transform, every haptic event with its handles and where it was routed, and
  every session event (tracking state changes, resets, room solver, plug/unplug).
- `lighthousedb.json`: the SteamVR database the host frame was aligned to.

Write down roughly when it happened ("hands flew at ~2 min in, then again when I
turned around"). That's enough: the timestamps in `driver.jsonl` line up with the
raw input.

### Replaying a recording quickly

`lhtool session CAPTURE --from S --to S --only SERIAL --trace OUT.jsonl` replays a
window (capture seconds) of one device as fast as the CPU allows and writes every pose
at 100 Hz of capture time. `tools/eval_replay.sh CAPTURE OUTDIR` scores a whole
recording in parallel windows (see `tracking_improvements.md`).

## Multi-station captures (ai/driver/multistation_plan.md, step 7)

Check the stations' mode live first (records nothing; also works on a file):

```sh
./build/lhcapture --check
./build/lhcapture --check captures/ms-n2/input.lhraw
```

Each second, per device: where each flash sits in the index-event group, how often it
is seen, and after what share of events that station sweeps. Expected:

| mode | clusters |
|---|---|
| stock A/b/c | `@0` 100 %, sweeps 50 % · `@417` 100 %, sweeps 50 % |
| multi N=2, MASTER+ACTIVE | `@0` 100 %, sweeps 50 % · `@250` ~50 %, sweeps 50 % |
| multi N=4, slots 0+2 | `@0` 100 %, sweeps 25 % · `@500` ~25 %, sweeps 25 % |
| policy ALL | slaves seen 100 % |

Then record a guided capture (~2 min of timed phases, each marked in `markers.jsonl`;
`--config` is stored in `session.json`):

```sh
./build/lhcapture captures/ms-n2 --scenario multi-n2 --config "N=2 master=<serial> slot1=<serial> MASTER+ACTIVE"
```
