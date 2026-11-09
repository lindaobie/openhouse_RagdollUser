# CDisambiguatorTDM (Lighthouse 1.0) and OOTX

Port: `src/tracking/disambiguator_tdm.{hpp,cpp}`, `src/tracking/ootx.{hpp,cpp}`.
Status: **bit-exact** vs the original on `captures/hmd_static_20s.lhraw`
(`tests/oracle/test_tdm_diff.cpp`: 2025 frames, 12 base configs, identical return flags
on all ~129k pulse/advance calls).

## Object
Factory `0x323e00(const Cfg*, int sensorType)`, `new(0x910)`, vtable `0x4711d0`.

| Slot | Addr | Method |
|---|---|---|
| 2 | 0x321630 | SetSensorType(int) → width offset |
| 3 | 0x321620 | GetSensorType |
| 4 | 0x3215c0 | SetConfig(cfg) |
| 5 | 0x3228a0 | GetStats(out, reset) |
| 6 | 0x321640 | StatsString(reset) — "TDM total=…" |
| 7 | 0x3214e0 | GetMode → 0 (TDM) |
| 8 | 0x328610 | OnPulse(sensor, t, width) → flags (bit0 config, bit1 frame) |
| 9/10 | 0x3214c0/d0 | LH2 words (no-op) |
| 11 | 0x3285a0 | Advance(t) |
| 12 | 0x321ca0 | HasOutput |
| 13 | 0x321a60 | Reset |
| 14 | 0x3214f0 | PeekBaseConfig |
| 15 | 0x3237a0 | PopBaseConfig(cfg, dyn) |
| 16 | 0x324740 | PopFrame(frame) |
| 17 | 0x3228e0 | PopEvent({double t; int type}) |
| 18 | 0x324860 | PopLog(string) |
| 19 | 0x321540 | EarliestPendingTime |

Config bytes (from `0x3cb080`, `disambiguationdebug` = 0): `+8 rightSync=1, +9 emitBeforeOotx=0,
+0xa debug, +0xb 1, +0xc int maxQueue=16, +0x10 flag`.

Layout (offsets into the object): idealPeriod 0x18 (1/60), period estimate 0x20, clusters
0x28, lastClusterCheck 0x40, lastSyncTime 0x48, nextUniqueId 0x50, nextExpectedSync 0x58,
scanTime 0x60, pending sync candidates 0x68, 32 per-sensor pulse stores 0x80 (0x38 each:
starts, ends, cap=256), bases 0x780 (0x1d8 each), sync detector 0x798 {sensorType, widthOffset},
avgNotSkip 0x7a8, syncCount 0x7ac, guardBand 0x7b0, config deque 0x7b8 (0x48), frame deque 0x808
(0x68), log deque 0x858, event deque 0x8a8 (0x10), counters 0x8f8.. (invalid_long, bad sensor,
runt, total, sync_miss).

## Sync width classes (0x3e2fc0)
`w = 62.5 µs + 10.417 µs·(skip·4 + data·2 + axis) + sensorTypeOffset`, accepted within
`[w − 6.25 µs, w + 2.083 µs]`, valid range `[56.25 µs, 137.5 µs] + offset`.
Sensor type offsets (0x3e2e20): types 4–7 2.604 µs, 8 1.354 µs, 9 1.979 µs, 10 1.458 µs,
11 1.042 µs, else 0 (Index HMD reports type 0, controllers type 9).

## Pipeline
`OnPulse` = CheckSyncLost → store/cluster → UpdateBases → ScanSyncCandidates →
ProcessExpectedSync → SanityCheck (reset on failure). `Advance` = same without store/UpdateBases.

1. **Clustering** (in OnPulse): sync-width pulses → phase `fmod(t, 1/60)·60`; join a cluster
   within 1/90 cycle, else start one.
2. **UpdateBases** (0x325260, every 0.125 s while < 2 bases): cluster pairs half a cycle apart
   (±1/360) with ≥5 hits and ≥10 % of the best pair; skip if aligned (±1/120) with an existing
   base; axis = majority of the sync classifications at that phase over the last 10 periods;
   new base gets two OOTX rings (358 and 341 slots) and a unique id.
3. **ScanSyncCandidates** (0x323430): every half period around the predicted sync (±P/30)
   gather sync candidates (0x322e50: pulses from all sensors within 4 ticks are merged,
   sorted by start, overlaps absorbed, classified).
4. **ProcessExpectedSync** (0x3273e0): once t ≥ next + 1.05·P (rightSync) match candidates to
   bases (phase ±1/360 of the base's per-axis period); update avg period (gain 0.03), global
   period (gain 0.01), jitter (≥1000 ticks), frame counter, OOTX bit. The base whose sync has
   skip=0 is the sweeping one; the other base's phase is snapped to 0/±1 slot of 1/2400 s.
   Right sync = single candidate near sync + avgPeriod. Samples: every pulse starting within
   `[sync + P/12, sync + 5P/12]`, angle `2π·((start+end)/2 − sync)/(next − sync)`.
5. **SanityCheck** (0x324c00): ≤ 2 bases; not both mode 1 and 3; each base's two axis syncs
   half a period apart (±1/120); different serials; bases within 1/30 period of each other.

## OOTX
- Majority ring (0x3b72f0): per slot `[zeros, ones]` counters, majority bit, decided count;
  every matched sync pushes its data bit (missed sync pushes 0xff = no vote, still advances).
  Decode attempted when a slot becomes decided and all slots are decided.
- Framer (0x3dfa30): ≥17 zeros, '1', then 16-bit words each followed by a '1'. Bad stuffing
  bit → resync keeping trailing zeros. Length u16 at bit 18, payload from bit 35, CRC32 (zlib)
  after the padded payload. Valid (length, version): (14,1)(16,2)(23,3)(31,4|5)(33,6)(41,7)(43,8).
- Base config (0x3b7c60): serial = u32 @2, hw = @0xf, version = @0 & 0x3f, cal per axis a:
  half-floats at `6+2a + {4,0,10,0x11,0x15,0x1b,0x1f}` = tilt, phase, curve, gibphase,
  gibmag, ogeephase, ogeemag. Verified equal to SteamVR's `lighthousedb.json`.
- Dynamic state (0x3b6f00): firmware version, normalized accel dir (int8/127 @0x14..0x16),
  mode (= mode_current+1), faults.
