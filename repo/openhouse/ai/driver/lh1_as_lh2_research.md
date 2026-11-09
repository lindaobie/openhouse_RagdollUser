# Can a 1.0 base station pose as a 2.0? (research, 2026-10-22)

Goal: make an LH1 base (lhbs firmware) emit light that tracked devices decode as a
Lighthouse 2.0 base: up to 16 independent channels, no sync flash, no TDM sharing, and
support for LH2-only devices. Verdict: **plausible with our own driver and a small
hardware change, impossible as a pure firmware change, and not something stock SteamVR
would accept.** Nothing here has been tested on hardware yet.

## What LH2 light is

From libsurvive (`libsurvive/src/lfsr_lh2.c`, `survive_process_gen2.c`,
`survive_reproject_gen2.c`) and Bitcraze's reverse engineering:

| | LH1 (our bases) | LH2 |
|---|---|---|
| Rotors | 2, one line laser each (horizontal and vertical sweep) | 1, two laser planes tilted +-30 deg (pi/6) |
| Rotation | 60 Hz (our PLL; a parameter) | 50.05-54.12 Hz, one frequency per channel (16) |
| Timing reference | IR sync flash (LEDs on a 1.8432 MHz carrier) | none: the beam itself carries its phase |
| Beam | plain on/off | BMC-coded 17-bit LFSR at 6 Mchip/s (48 MHz / 8) |
| Base identity / OOTX | flash width bits | which of the channel's 2 polynomials is used (32 in all) |
| Which plane | which rotor / sync axis bit | which half of the revolution (`angle > pi`) |

The device (TS4231 sensors and an FPGA) reports raw LFSR samples and timestamps; **the
host** finds the polynomial (= channel and OOTX bit) and the offset (= rotor phase),
which gives the angle. So decoding and geometry live in the driver, which we own.

## How the pieces map

* **Planes by half revolution:** the LH1 rotors already sweep in alternate halves, which
  is exactly how LH2 tells its two planes apart. One LH1 rotor = one LH2 plane.
* **Geometry differs.** LH2 planes rotate about one axis, tilted +-30 deg. LH1's vertical
  sweep turns about a different axis, which no LH2 calibration can describe. **Stock
  SteamVR would compute wrong angles.** Our driver would need its own measurement model:
  LH2 decoding into the existing LH1 sweep model (axis 0 = rotor 0 plane, axis 1 =
  rotor 1 plane).
* **Rotation speed:** set the rotors to the chosen channel's frequency (50-54 Hz) with
  the existing PLL period parameter. The mechanics have to hold lock there, which is
  untested.
* **Phase:** restart the LFSR on each rotor index edge, so the offset is the rotor
  angle. The index edge is already captured by CT32B0 in hardware.
* **Sync LEDs:** off. LH2 has none, and a device that sees LH1 flashes may switch modes.
* **OOTX:** switch between the channel's two polynomials per bit. The LH2 OOTX layout
  differs from LH1's, but both ends are ours.

## The hard part: putting 6 Mchip/s on the laser

The laser is switched by a plain enable pin per diode (P1_12, P0_16), driven by CT32B0
match events; the LTC6904 carrier only drives the sync LEDs. The ISL58303 is a dual
laser-diode driver for optical disc drives, a part made for fast write pulses, so its
enable path is probably fast enough. That still has to be measured. Options for the
source of the stream (BMC: up to 12 M transitions/s, steady):

1. **MCU in software:** no. 4 cycles per transition at 48 MHz, with rotor and
   timebase interrupts running.
2. **MCU's SSP (SPI):** possible. Shift a precomputed stream (both polynomials, about
   32 KB, fits the 128 KB flash) out at 12 Mbit/s. The LPC11U37 has no DMA, so an
   interrupt refills the 8-word FIFO about every 10 us during each sweep, which is
   tight next to the PLL. SSP's data-out pins (P0_9, P0_21, P1_22) aren't the
   laser-enable pins, so this needs a **bodge wire** (check the pin-mux table in the
   datasheet before choosing).
3. **External chip** (e.g. RP2040 with PIO, a few dollars): generates the stream in
   hardware, LFSR restarted on the index edge, wired to the enable lines. The most
   robust choice; the MCU keeps doing rotor, APC and OOTX.

Eye safety: the beam stays Class 1 only while it sweeps. The existing interlock (lasers
only with locked rotors) must stay in front of any modulation path.

## Device side and driver work

* Devices: Index controllers and Vive Tracker 2018/3.0 support LH2; first-generation
  trackers don't. Tundra trackers are often described as LH2-only (not verified here):
  if so, they are the main gain. An LH1 and an LH2-style base
  in one room may confuse device modes: Bitcraze notes V1 and V2 can't be mixed, and how
  Valve's devices behave with both is unknown. Plan for all-LH2-style rooms.
* openhouse: today it reads LH1 pulses only. It would need the LH2 light reports from the
  USB/radio layer, LFSR decoding (port libsurvive's `lfsr_lh2.c`), and the LH1-geometry
  model fed by LH2 angles.

## Gains vs today's TDM multi-station

* Each base sweeps every revolution instead of 1 in N (4 bases: 4x the light per base).
* No master/slave timing, so the slave axis-mislabel issue (`ai/firmware/MULTISTATION.md`)
  and optical sync loss go away.
* Up to 16 channels, and LH2-only devices.

## Cheapest experiments, in order

1. **Laser enable bandwidth:** toggle one enable pin at 1-12 MHz (bodge from SSP or an
   external generator) with the rotor locked; check the beam with a fast photodiode or
   scope.
2. **Does a device report LH2 samples?** Emit one channel's LFSR on rotor 0 only, sync
   LEDs off, and log what an Index controller sends over USB (libsurvive's `driver_vive.c`
   shows the report layout).
3. **Rotor at about 50 Hz:** run the PLL at a channel frequency and check lock and jitter.
4. **Decode on the host:** port the LFSR decode and recover the rotor phase from the
   offsets; then the angle model.

If step 1 or 2 fails, the idea is dead for this hardware; both take hours, not weeks.

Sources: libsurvive (in-tree), Bitcraze's [Lighthouse V2 status update](https://www.bitcraze.io/?p=8814),
[V2 overview](https://bitcraze.io/?p=8596) and
[system overview](https://www.bitcraze.io/documentation/repository/crazyflie-firmware/2022.05/functional-areas/lighthouse/system_overview/),
and `ai/firmware/HARDWARE.md` for the LH1 board.
