# CSwSyncDetectSource (per tracked device) — what the tracker actually receives

Vtable 0x473398 (ILighthouseSource), secondary vtables 0x473490 (IMU callback, this-8)
and 0x4734e0 (optical callback, this-16). Slot maps in interfaces.md.

## Port status

`src/tracking/tracking_source.{hpp,cpp}` implements a transport-independent,
per-device LH1/TDM source. Call `OnImu` / `OnLightPulse` with normalized device-time
events, then pop IMU, frames, base configs, TDM events, and logs. One instance is
required per device, constructed with its sensor type (HMD 0, Index controller 9).
The class serializes push/pop operations with a mutex. It does not own transports,
convert to host time, produce poses, implement a waiting worker thread, or route
multiple device IDs. The future tracker/input-sink adapter must provide that wiring.

Unit tests check float quantization, FIFO/peek behavior, overflow, negative startup
timestamps, and device isolation. Capture tests compare its frame/config output to
the original TDM with independently transcribed source advance conditions: 2,025
HMD frames / 12 configs and 825 controller frames / 6 configs match exactly.
This does **not** instantiate the full original CSwSyncDetectSource; auto-selection,
restart behavior, and event/log equivalence are not verified. The existing capture
test's startup-gap trimming still applies.

## IMU path
- IMU callback (0x3e2d90 → push 0x3c9d60): sample stored as `{double t; float acc[3];
  float gyro[3]; u32 flags}` (**float precision**) in a deque capped at 1000 (oldest dropped);
  `+0x108` = latest IMU time. The tracker pops these (slot 10).
- Advance cadence (same function): read `lastPulseImuTime (+0x200)` and the previous latest
  IMU time **before** pushing. If `lastPulseImuTime != previousLatestImuTime`
  (a pulse after the previous IMU sample makes them equal, suppressing advance), and
  `t > nextAdvanceCheck (+0x208)` (or it is 0): if `lastPulseImuTime + 0.02 < t` call
  `Advance(t − 0.02)` on primary and alt disambiguators; `nextAdvanceCheck = t + 0.05`.
- OnPulse (0x3e2880): `lastPulseImuTime = latest IMU time`, feed alt (+0x248) then primary
  (+0x240) disambiguator; if a call returns bit0 (new base config) run selection (0x3e11c0).

## Disambiguator selection (auto mode 0xfe, the default)
- Start: primary = TDM (factory mode 0), alt = Framer (mode 1).
- Selection (0x3e11c0) when a disambiguator delivers a base config: rotor count from
  hw_version table (0x1a40e0); for 2-rotor (LH1) bases, if primary or alt is TDM → select TDM,
  else select the delivering disambiguator's mode. Selecting destroys the other one.
- Frames and base configs are popped **only from the primary**, which is TDM from the start.
  ⇒ For LH1 the Framer never influences what the tracker receives; it is not ported.

## Frames (slot 9, 0x3e0720)
Pops the primary's frame, then `0x3a1930` records per-base bookkeeping (speculative base
identity from partial OOTX, "Speculative Start Collision") and fills `serial` only when it
is 0. TDM never emits serial 0 frames (`emitBeforeOotx = false`), so frames reach the
tracker unchanged, in **device time**.
