# Internal interfaces (vtable slot maps)

Slot = byte offset / 8. Addresses are Ghidra addresses. Full table: `re/vtables.tsv`.

## ILighthouseSource — what the tracker consumes
Implemented by `CLighthouseSource` (vtable 0x4729a0, IMU-only) and
`CSwSyncDetectSource` (vtable 0x473398, adds optical via disambiguators).

| Slot (off) | CLighthouseSource | CSwSyncDetectSource | Meaning |
|---|---|---|---|
| 7 (0x38) | 0x3c9330 | 0x3e0660 | `Wait(ms)` condvar wait |
| 8 (0x40) | 0x3c8ac0 →0 | 0x3e05e0 | `PopBaseConfig(cfg, dynState)` → disambiguator slot 15 |
| 9 (0x48) | 0x3c8ad0 →0 | 0x3e0720 | `PopOpticalFrame(frame*)` → disambiguator slot 16, then time-maps via 0x3a1930 |
| 10 (0x50) | 0x3c8eb0 | same | `PopImuSample(sample*)` — 40-byte record `{double t; float acc[3]; float gyro[3]; u32 flags}` from deque @+0x120 |
| 11 (0x58) | 0x3c8cf0 | 0x3e2690 | `PopEvent({double t; int type})`; type 1 = disconnected (t = now − last) |
| 12 (0x60) | 0x3c98c0 | 0x3e2800 | `PopLogMessage(string*)` |
| 13 (0x68) | 0x3c8af0 | 0x3e25f0 | `PeekNextImuTime()` |

Tracker thread loop: `CLighthouseDirect` slot 2 = 0x3705a0 ("LighthouseDirect").
Per iteration: Wait(100) → drain events (0x357fa0) → drain base configs (0x350240) →
pop IMU (0x353710) → pop optical frame into `this+0x338`, interleave IMU samples by
time, process frame (0x370450) → 0x358300 / 0x350c90 → publish pose (0x351700).
**Our port does not replicate this thread; the core is driven synchronously.**

## CLighthouseOpticalThread::ICallback (transport → source)
Implemented by CSwSyncDetectSource secondary vtable 0x4734e0 (thunks `this-0x10`).

| Off | Impl | Signature | Meaning |
|---|---|---|---|
| 0x10 | 0x3e29c0→0x3e2880 | `(int sensor, double t, double width)` | LH1 pulse → primary (+0x240) and alt (+0x248) disambiguator slot 8 |
| 0x18 | 0x3e0880 | `(double t, int s, int flag, u32 a, u32 b)` | 0x27 raw entry — no-op |
| 0x20 | 0x3e1690→0x3e1480 | `(double t, int n)` | demod count; > 400/s within 0.1 s while mode==0xfe(auto) → switch to SyncOnBeam (mode 2) |
| 0x28 | 0x3e1940→0x3e1890 | `(double t, int ch, int b26, int b27)` | LH2 0x28 word kind A → disambiguator slot 9 (+0x48); sets `+0x220 = t+11` |
| 0x30 | 0x3e1a00→0x3e1970 | `(double t, int ch, int v)` | LH2 0x28 word kind B → disambiguator slot 10 (+0x50) |
| 0x38 | 0x3e2bc0→0x3e2a20 | `(int type, int arg)` | event; 0xe = SOB channel conflict |

## IDisambiguator
CDisambiguatorFramer vtable 0x471030, CDisambiguatorSyncOnBeam 0x4710f8,
CDisambiguatorTDM (anonymous namespace).

| Slot | Framer | SyncOnBeam | Meaning |
|---|---|---|---|
| 7 (0x38) | 0x30ff90 | 0x318270 | `GetMode()` (2 = SOB) |
| 8 (0x40) | 0x317ba0 | 0x318260 | `OnPulse(sensor, t, width)` → bool |
| 9 (0x48) | 0x30ff70 | 0x3207e0 | LH2 kind-A word |
| 10 (0x50) | 0x30ff80 | 0x31e610 | LH2 kind-B word |
| 11 (0x58) | 0x317b70 | 0x31ff40 | `Advance(t)` |
| 12 (0x60) | 0x310060 | 0x318120 | `HasOutput()` |
| 13 | 0x310610 | 0x31bf90 | |
| 14 | 0x30ffa0 | 0x318190 | |
| 15 (0x78) | 0x314e70 | 0x318540 | `PopBaseConfig` |
| 16 (0x80) | 0x312af0 | 0x31cf20 | `PopFrame` |
| 6 | 0x310260 | 0x3186b0 | stats print ("Framer total=…") |

## Ceres functors (CCeresFunctor<F, double, N, 0…>)
Each vtable: [0,1] dtors, [2] Evaluate with Jacobians (Jet<double,N>), [3] Evaluate
residuals only (double), [4] NumResiduals (1, or 3 if component index < 0), [5] ParameterBlockSize.
See measurement_models.md.
