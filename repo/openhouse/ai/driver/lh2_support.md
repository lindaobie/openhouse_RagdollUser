# Lighthouse 2.0 support (2026-10-22)

Status: working end to end on recordings from a friend's room (4 LH2 bases, channels
1/2/3/5; Index controllers and Vive Tracker 3.0). Not yet tested live.

## Pipeline

1. **Light mode.** Index controllers and trackers report LH1 pulses until the host sends
   optical sub-protocol 1 (`UsbTransport::SetOpticalSubProtocol`, 0x87 TLV type 1). Stock
   SteamVR does this for LH2 rooms; the driver does it too (Light mode below). The
   recordings were made with `lhcapture --lh2`, which sets it.
2. **Words.** Report 0x28 / wireless sub-protocol 1 carry beam words (once per
   revolution: rotor phase reference, bit 26 = OOTX bit, bit 27 = "generation") and sensor
   words (sensor id, hit time). Parsing existed already (`report_parser.cpp`).
3. **`sob::SobDisambiguator`** (`src/tracking/disambiguator_sob.*`), after stock
   `CDisambiguatorSyncOnBeam` (vtable 0x4710f8; beam word 0x3207e0, sensor word 0x31e610,
   frames 0x31f340; decompiled into `re/decomp_sob/`, local only). Per channel: nominal
   period from table 0x1a1ce0 (48 MHz ticks, 50.05-54.11 Hz), smoothed measured period;
   hit angle = 2π (t − beam word) / period; first half turn = plane 0 (frame axis 0),
   second = plane 1. OOTX from bit 26 through the LH1 framer (`ootx::DecodeFramer`);
   ootx version 8 payloads decode, calibration matches SteamVR's database exactly.
   OOTX also goes into a revolution-indexed majority ring (443 bits, missed revolutions
   keep their slot), as stock's bit buffer 0x3b7430/0x3b8140.
   Frames start once the channel's base is known: from OOTX, from SteamVR's database
   (`dynamic_state.sobChannel`, 0-based), or from another device (Session shares the
   channel map; `TrackingSource::SetKnownSobChannels`).
4. **Model.** `model::Lh2Angle` / `BaseAngle` (`lighthouse_model.hpp`): stock gen2
   evaluate 0x3760e0 / 0x375780, matches the original to 1e-12 for model types 2-5
   (oracle test `Lh2AngleMatchesOriginal`). `BaseConfig::model` carries the type to the
   filter (`SensorAngular`), pose solver and room solver. Type per base hardware version
   as stock (table 0x1a2840 at hw - 7, set by the functor factories 0x34a940/0x34b110):
   2.0 bases (hw 14/15) are **type 5**. Real data agrees: only types 3 and 5 fit, per-base
   solves of a still controller agree within 5 cm through SteamVR's room.

## Results (2026-10-22, ours vs `LH_STOCK_FILTER`)

| Recording | valid | coast | resets | jitter |
|---|---|---|---|---|
| `-175718Z-93717` (1 controller, 31 s) | 99.8 vs 97.8 % | 0.9 vs 88 % | 0 vs 0 | 4 um vs 1 mm |
| `-171923Z-81871` (5 devices, 160 s) | 97.3 vs 89.6 % | 5.2 vs 60 % | 0 vs 53 | 0.1 vs 6.8 mm |
| `-170505Z-79354` (1 controller, 160 s) | 99.3 vs 97.8 % | 0.2 vs 59 % | 0 vs 2 | 26 vs 250 um |

What it took, in order of effect: SteamVR's `sobChannel` is 0-based (read as 1-based it
mapped bases to the wrong channels: "light keeps contradicting"); a base known by channel
is announced to the session as seen (else the room never activated: "acquiring" forever);
`Advance` runs 0.25 s behind the IMU (LH2 words lag it; at IMU time every revolution
looked overdue, frames got predicted syncs and the filter drops those); the next
revolution's hits can precede its beam word (kept, not a missed word); frames released
in time order across channels (all bases sweep at once); the OOTX majority ring. The
LH2 gravity sign as reported is right (flipped: 37 resets).

## Light mode (`SessionImpl::UpdateLightMode`, live only)

Start LH2 if SteamVR's database lists only 2.0 bases. Once bases of one kind only have
been seen, keep every device in that mode (re-send every 5 s while dark). Otherwise a
device without usable light (LH1 sync-width pulse, LH2 beam word) for 2.5 s tries the
other mode. In LH1 mode 2.0 bases give stray pulses without sync flashes; in LH2 mode
1.0 bases give nothing. Not yet tested on hardware.

## Open items

* Recheck the unflagged fly-aways seen before the fixes above (tracker LHR-002169E6 2-4.5 m
  low for > 1 % of its tracking time in `-171923Z-81871`).
* Live test of the light-mode switching (LH1 room: must stay LH1; LH2 room: must switch).
* Not handled: generation (bit 27) changes beyond a channel reset, channel conflicts
  (event 0xe), Tundra trackers. Mixed LH1/LH2 rooms (a device reports one mode at a time):
  behind a switch, `mixed_stations.md`.
