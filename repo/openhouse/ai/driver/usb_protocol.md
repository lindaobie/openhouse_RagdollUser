# USB / HID protocol (as parsed by driver_lighthouse.so)

All addresses are Ghidra addresses (ELF VA + 0x100000).

## Transport

- hidapi (hidraw backend) is statically linked: `hid_enumerate` 0x308520,
  `hid_open_path` 0x308950, `hid_read_timeout` 0x308af0, `hid_get_feature_report` 0x308bc0.
  Thin wrappers: read 0x334150, get_feature 0x334390, open 0x335800, enumerate 0x337930.
- `CLighthouseUsbThread::Run` (0x3cb310, thread name "LighthouseUSB") polls every open
  endpoint, reads up to 10 reports of ≤64 bytes per pass and calls
  `endpoint->OnReport(double hostTime, u8* data, int len, bool lastInBatch)` (vtable slot 2).
  hostTime = host monotonic seconds at read.
- Endpoints are `CLighthouseThreadBase` subclasses:
  - `CLighthouseIMUThread` (vtable 0x472c48) OnReport = 0x3d1a60
  - `CLighthouseOpticalThread` (vtable 0x472d38) OnReport = 0x3cf250

## Device timestamps

- Device clock is **48 MHz** ticks (`1/48e6` = const 0x1a96f0).
- Timestamps are 24, 28 or 32 bit and unwrapped to 64 bit by `UnwrapTicks`
  (0x3ced40): `v = (last & ~(2^n-1)) | raw; if v < last - 2^(n-1) v += 2^n; if v > last + 2^(n-1) v -= 2^n;`
  the reference `last` is only advanced when `n == 32` or `v > last`.
  **One unwrapper per device**, shared by IMU and optical endpoints, mutex-guarded
  (`endpoint+8` points at it). The 32-bit IMU stamps keep the reference fresh so the
  24-bit optical stamps can unwrap.
- Host↔device offset (`ClockSync`, 0x3cf2c0), updated from the last IMU sample of
  each 0x20 report when `lastInBatch`:
  ```
  hostT = readTime - 0.0009            // 0x1a9a20 USB latency
  err   = (hostT - devT) - offset
  if offset==0 || hostT - lastUpdate > 0.5 || |err| > 0.05 : offset = hostT - devT
  else offset += (err < 0 ? 0.025 : 0.0025) * err   // follows decreases fast, increases slow
  ```

## IMU endpoint (CLighthouseIMUThread::OnReport 0x3d1a60)

| ID | Meaning |
|---|---|
| 0x20 | Wired IMU: 3 samples (decoder 0x3d0a00) |
| 0x23, 0x24 | Wireless (Watchman dongle) packet → CWirelessDecoder (0x3d1920) |
| 0x26 | Wireless controller disconnect (`[1]==1`) |
| 0x27, 0x28, 0x2a | Wireless status/time-sync variants (event 5/6 into IVrcCallback) |
| other | stock feature/status handler |

### 0x20 layout
```
[0]      0x20
[1+17*i] int16 accel[3], int16 gyro[3], u32 timestamp(32-bit ticks), u8 seq   i=0..2
```
Samples are emitted oldest-first (start at min timestamp, ring order) and only if
newer than the last emitted (`endpoint+0x50`). Per sample (0x3d07c0):
- accel m/s² = raw × `accelScale[accelRange]`, table 0x1a40a0 = `2g·9.80665/32768 × 2^k`
  (0.000598568…, ×2, ×4, ×8)
- gyro rad/s = raw × `gyroScale[gyroRange]`, table 0x1a4080 = `250°/s·π/180/32768 × 2^k`
- flags bit i (0..5) set when component i is saturated (`raw` ∈ {−32767, 32767, −32768}).
- range indices fetched from device ("GyroRangeMode/AccelRangeMode" log), -1 = unknown → sample dropped.
- After all 3 samples are new: IVrcCallback event 0xb.

## Optical endpoint (CLighthouseOpticalThread::OnReport 0x3cf250)

Callback target is `ICallback` at `endpoint+0x50` (implemented by CSwSyncDetectSource,
secondary vtable 0x4734e0), plus an aux callback at `endpoint+0x68`.

### 0x21 — LH1 pulses, 7 × 8 bytes
```
[1+8i] u8 sensor (0xff = empty), u8 ?, u16 width_ticks (0xffff = unknown → -1.0), u32 ts (28-bit)
```
### 0x25 — LH1 pulses, 9 × 7 bytes
```
[1+7i] u8 sensor, u16 width_ticks (0xffff = empty), u32 ts (28-bit)
```
Both go through `OnPulseRaw` (0x3cec20):
- width < 0 → event(3)
- sensor < 0xf7: if 0x1a9bb8 < width < 0x1a97f0 and not rate-limited (0x1a9768 s) → event(0xf);
  then `cb->OnPulse(t, width, sensor & 0x1f)` (slot +0x10)
- sensor ≥ 0xf7 → aux `cb68->(+0x10)(0xfe - sensor, 0)` (buttons/aux)

### 0x27 — 4 × 13 bytes
```
[1+13i] u8 s (0xff empty; s&0x1f sensor, s>>7 flag), u32 ts(28-bit), u32 a, u32 b
```
→ `cb->(+0x18)(t, s&0x1f, s>>7, a, b)` (CSwSyncDetectSource: no-op);
if s>=0 and (a & b) != 0 → `cb->(+0x20)(t, 1)` (demod counter).
If len ≥ 0x3b and `*(u32*)(p+0x37) == 0x0de40daa`: alternate format via 0x3cef10, then
`cb->(+0x20)(t(p+2), u16 @0x35)`.

### 0x28 — LH2 tagged stream (Index HMD, Watchman v3)
```
[1] payload length, payload starts at [2]
loop:
  b = *p
  if b & 1:   control byte
     (b&0xf)==1 : channel = b>>4           (1 byte)
     (b&0xf)==5 : cb->event(0xe, b>>4)     (1 byte)  "sync on beam channel conflict"
     (b&0xf)==3 : u32 w; aux68->(+0x10)(t=(w>>4)&0xffffff, w>>28, 0)   (4 bytes)
     else       : skip 1 byte
  else:        u32 w (4 bytes), requires channel != -1
     w & 2 == 0: t = (w>>2)&0xffffff;           cb->(+0x28)(t, channel, (w>>26)&1, (w>>27)&1)
     w & 2 != 0: t = (w>>3)&0xffffff; if w&4: t += 0.5 tick (1/96 MHz)
                                                cb->(+0x30)(t, channel, w>>27)
```
24-bit timestamps. Field meaning (which word carries the sensor ID) is still being
confirmed against the SyncOnBeam disambiguator (see interfaces.md).

## Observed on this machine (2026-10-16, SteamVR NOT running)
A histogram of HID report IDs per hidraw node over 5 s:

| Node | Device | Reports |
|---|---|---|
| hidraw10 | 28de:2000 Lighthouse FPGA RX (Index HMD, IMU iface) | 0x20 ×4703 (len 52) ≈ 940 reports/s ≈ 1 kHz IMU |
| hidraw14 | 28de:2000 (Index HMD, optical iface) | 0x25 ×3908 (len 64) |
| hidraw7, hidraw9 | 28de:2101 Watchman Dongle | 0x23 (len 30), 0x24 (len 59) |
| hidraw5, hidraw8 | 28de:2102 Watchman Radio / Kuroshin Dongle | nothing |

The reference setup uses LH1 bases, so the HMD's raw 0x25 pulse stream is expected.
Earlier speculation that this implied a missing LH2 startup switch was incorrect.

## Standalone configuration retrieval

`device_config.*` implements the standard compressed config feature stream:
GET_FEATURE 0x10 starts/resets the stream and returns the advertised compressed
length (bytes 1..2, big-endian); repeated GET_FEATURE 0x11 returns
`[id, chunkLength, chunkBytes...]`, terminated by a zero-length chunk. The result
is zlib-inflated. Limits: 64 KiB compressed/decompressed, 2,048 chunk requests,
chunk lengths checked against the actual report size. Truncation, malformed
chunks, invalid compression, and non-object output are rejected without changing
the caller's output string.

This reader was cross-checked with the public protocol layout in the local
libsurvive tree and tested using synthetic compressed streams. It is **not yet
hardware- or original-function-oracle verified**. `UsbTransport` prefers cached
config JSON and only tries the device reader when that is missing; this is
controlled by `UsbTransportOptions::fetchConfigIfMissing`.

Range-mode feature 0x01 now retries up to 100 times, as recovered previously.
Transport Stop sends one detach notification per device; a subsequent Start clears
old timestamp/protocol state. Endpoint enumeration and automatic reconnect while
already running remain separate unfinished work.
