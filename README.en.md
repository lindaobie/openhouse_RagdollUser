# openhouse · RagdollUser Edition

[中文](README.md) | **English**

> Based on [YusufOzmen01/openhouse](https://github.com/YusufOzmen01/openhouse) (MIT).
> Runs **four** HTC Vive 1.0 base stations at once (the hardware only has four channels),
> and ships a Windows GUI for the whole workflow.

---

## What this is

```
unzip → double-click openhouse-setup.exe
```

Self-contained: no .NET runtime needed. From the GUI you can flash firmware, install the driver, read station status, look at the room in 3D, and check multi-station sync quality.

**Bilingual**: the title-bar button switches between `EN` and `中`, and the choice is remembered.

---

## Getting started

### 1. Flash the firmware

```
GUI → Base stations → Flash firmware
```

Four steps into ISP mode (power off → hold the button on the back → plug in micro-USB → power on, then release). The page detects the CRP volume itself. It insists on a stock-firmware backup first: without one, flashing and restoring are both locked.

> **A station already showing the red LED must be restored to stock first, powered for a few seconds, then flashed again.**
> Flashing straight over it leaves a station that has no red LED but does not track. That is measured, not guessed.

### 2. Install the driver

```
GUI → Driver → Install driver
```

It closes whatever is holding the driver DLL (Steam itself holds it too). Valve's original driver is kept as `.stock` and can be restored from the same page.

### 3. Check placement

```
GUI → Base stations → Map           3D view of position and beam direction
GUI → Tracking → Tracking quality   whether the stations share one timebase
```

The tracking page does not care where the tracker is. It shows whether the stations are taking turns in one timebase: offset, seen ratio, turn, sync state. A missing or stumbling station stands out.

---

## Layout

```
openhouse_with_GUI\
├── openhouse-setup.exe          the GUI, self-contained single file
├── web\                         UI assets
├── packages\
│   ├── driver\                  driver, tools, install scripts
│   └── firmware\                firmware images, flashing scripts
├── docs\                        analysis and reports
├── tools\                       capture and build scripts
├── README.md                    Chinese
├── README.en.md                 English (this file)
└── 启动说明.txt                  Chinese quick-start note
```

---

## Facts

| | |
|---|---|
| Upstream | `github.com/YusufOzmen01/openhouse` |
| Source baseline | upstream `aaaaffc` (2026-11-11) plus the patches below |
| Firmware | `lhbs-20261112-D930256358A9.bin`, 51,192 bytes (arm-none-eabi-gcc 13.2.1) |
| Driver | same source, MSVC + vcpkg static build, 4,528,128 bytes |
| Stations | Vive 1.0; four at a time (the firmware only has four channels) |

---

## What we changed

### Firmware

**No local patches.** Upstream added `repair_laser_currents()` on 2026-11-10: at boot, if a laser current is outside 80–230, it restores the real calibration from the **factory partition** and writes it back to the user partition. That is strictly better than the old `< 40 → 190` fallback, so the local patch is gone.

### Driver (relative to upstream `aaaaffc`)

| File | Change | Why |
|---|---|---|
| `src/transport/usb/usb_transport.{hpp,cpp}` | new `OnDeviceGone()` | Nothing cleared `receivers_[dongle].connected` when a device dropped off wirelessly, so SteamVR's pairing page kept saying the controller was connected; swapping devices required unplugging the receiver |
| `src/runtime/session.cpp` | `Base::lastSeen`, 8 s timeout | `seen` never went back to false, so a powered-off station kept showing "locating" forever |
| `src/openvr/driver_lighthouse.cpp` | `PublishState` 20 Hz → 60 Hz | The map page draws at the display rate; at 20 Hz two frames out of three were drawn from a stale pose, which read as drag |

The second one contradicts the `as requested` design note in the upstream docs (they deliberately keep stations listed), so it stays local.

### Tools

| File | Change | Why |
|---|---|---|
| `apps/lhcapture/main.cpp` + `sync_monitor.hpp` | report every 50 ms over a 0.8 s window, was 1 s over 4 s | The old numbers were slow averages that looked frozen |

### GUI (`shell/OpenhouseSetup`)

Flashing, driver install and the placement check started as three PowerShell scripts scattered across folders, with parameters you had to look up. They now live in one window: a C# WinForms shell hosting a WebView2 page.

| Page | What it does |
|---|---|
| Home | Three status cards (SteamVR / driver / stations) and what to do next |
| Base stations / Status | Channel, role, lock state, placement sigma, solve frames |
| Base stations / Flash firmware | Four-step ISP guidance; read-only check, flash, restore; forced replug before verifying |
| Base stations / Map | Live Three.js scene: station positions, beam cones, tracker |
| Tracking / Tracking quality | Reads raw pulses from the receiver and shows whether the stations share a timebase |
| Driver | Install / remove the openhouse driver, closing whatever holds the file |
| Log | In-memory plus an on-disk file, copyable, folder opens from the page |
| About | How it works |

**Things done deliberately**

- **Frameless window, corners left to DWM.** CSS `border-radius` only rounds the page content, not the window; `DWMWA_WINDOW_CORNER_PREFERENCE` is what actually rounds it.
- **Selected rows never change background.** Windows subpixel antialiasing makes glyphs look softer when the background changes, so selection is drawn by a separate sliding box.
- **Button labels sit 2 px high.** CJK ink sits low in the line box; centring it naively looks wrong.
- **Counters update in place.** Text nodes only — rebuilding the DOM throws away the scroll position and replaces the button between mousedown and mouseup, which loses the click.
- **Three separate logs.** Flashing, placement and driver each keep their own buffer, so a flash message never shows up on the placement page.
- **No way out after a write.** The modal asks you to unplug and replug, and will not close before that, so a half-written station cannot be mistaken for a finished one.

---

## Command line

```
packages\firmware\openhouse-basestation-firmware\Install-Firmware.bat   flash
packages\firmware\openhouse-basestation-firmware\Verify-Firmware.bat    verify only
packages\firmware\openhouse-basestation-firmware\Restore-Stock.bat      restore stock
packages\driver\openhouse-windows-x64\Install-Driver.bat                install driver
```

Building the firmware: `tools\build-firmware.cmd`. Needs `arm-none-eabi-gcc` 13.2.1, GNU Make 4.4.1, Python and the `sh` that ships with git.

---

## Known issues

| Issue | Layer | Status |
|---|---|---|
| Laser current ratchets down → red LED | firmware | fixed upstream (`repair_laser_currents()`) |
| Room never settles (a station locked before converging) | driver | fixed |
| Swapping a tracker needs a SteamVR restart | driver | fixed |
| A powered-off station lingers in the list | driver | fixed (`seen` expires after 8 s) |
| Flashing straight over a red-LED station → no tracking | firmware | cause unknown; the GUI can run restore → power a few seconds → flash in one go |
| One station hears no slave flashes while acting as master | firmware | not investigated; use another station as master |

---

## Notes

- Flashing writes the application area only (currently 51,192 bytes) and leaves the EEPROM copy from `0x1F000` alone. The serial number and laser calibration live in the chip's own EEPROM, separate from flash.
- The genuinely irreplaceable part is each unit's **EEPROM** (factory calibration). lhbs writes a copy into flash at `0x1F000` on every boot, so a station that has run lhbs can be identified from there.
- The stock driver is kept as `.stock` and can be restored from the driver page.
