# Sensor type and sync-width offset

The TDM disambiguator classifies a sync pulse by its width:
`62.5 µs + 10.417 µs·(skip·4 + data·2 + axis) + offset(sensorType)`, accepted in
`[w − 6.25 µs, w + 2.083 µs]`.

Offset per sensor type (`0x3e2e20`): types 4–7 → 2.604 µs, 8 → 1.354 µs,
9 → 1.979 µs, 10 → 1.458 µs, 11 → 1.042 µs, anything else → 0.

This offset is small but not cosmetic: a tracker whose syncs land 0.7–1.7 µs
above the nominal class **fails every classification at offset 0**, so no sync is
ever accepted and the device never produces optical frames.

## Where the type comes from

The stock driver constructs the TDM from the connection's sensor type and logs
`SwSyncDetect ... SensorType %d` (and `(selected N)` when the observer changed
it). Our port originally inferred it from the device config:

* `model_number` contains `"Knuckles ..."` for Index controllers → type 9;
* Vive trackers/wands only carry `model_name` (e.g. `"Vive Tracker MV"`), so they
  fell through to type 0.

Measured on LHR-0AC3E7C8 (Vive Tracker MV): observed sync widths are
65.1–65.3 µs, 75.6–75.7, 86.0, 96.4–96.8, 106.6–107.0, 117.0–117.4, 127.9,
137.8–138.3 µs — i.e. ~0.7–1.7 µs above the nominal classes. That matches the
type-9 offset, and with type 9 the same pulse stream decodes into frames.

## Current implementation

1. `TrackedObjectModel::FromJson` uses an explicit `sensor_type` field when the
   config has one, otherwise recognises `model_number`/`model_name` containing
   `"Knuckles"` (type 9) and falls back to 0.
2. `InferSensorTypeFromSyncWidths` scores the observed widths against the
   non-zero offsets and reports the best-matching type.

## Stock source of the sensor type (traced 2026-10-18)

- `003e0d90` returns `0x101` ("NotYetAvailable") until the device's HID wrapper
  holds a valid version report, then the low byte of the u32 at wrapper `+0xf8`
  (values > 11 become `0x100`, unknown).
- `003e16f0` re-polls while the TDM type is `0x101` and logs
  `TDM sensorType changed to: %d` once it arrives; the TDM is created first.
- `003cd640` fills wrapper `+0xd0..` from feature report 0x13 (63 bytes) or,
  for receivers (PID 0x2101/0x2102, gated by `ff 8c` in `003ccd80`), 0x05
  (52 bytes). `HidDevice_GetFeature` strips the report id, so in a hidapi
  buffer the hardware id is bytes `[41..44]` (LE) and the type is `buf[41]`.
  `003cd9c0` prints that word as `product %u rev %u.%u.%u`.

Verified on hardware: the wired HMD (LHR-0704E07A) returns hwid `0x80020100`
(product 128 rev 2.1.0) → type 0, matching the HMD oracle.

Resolved for wireless devices: receivers *do* answer report 0x05 for their
paired device, but relay it over the radio, so the first reads fail (hidapi −1).
Stock retries up to 100 times (`HidDevice_GetFeature(..., 0x05, 0x34, 100)`);
with retries the replies arrive within a few attempts. Verified on hardware:
controller LHR-13DA81D1 hwid `0x110e0009` → type 9, tracker LHR-0AC3E7C8 hwid
`0x84010004` → type 4, HMD `0x80020100` → type 0.

## Implementation

`UsbTransport::CompleteSetup` reads report 0x05 (up to 100 attempts) and adds
`"sensor_type"` to the device JSON, so captures carry it. `TrackedObjectModel`
then marks the type as known and `TrackingSource` skips the width inference.
`InferSensorTypeFromSyncWidths` remains only as a fallback for devices whose
report cannot be read (e.g. old captures).
