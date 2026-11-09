# Units with a stale `laser.*.current` in EEPROM come up red-blinking and never emit

**Component:** `basestation_firmware` (lhbs)
**Reported against:** openhouse 1.0.1 / firmware `lhbs-20261108` (build 2026-11-08 20:15:40)
**Hardware:** 1.0 base stations (LPC11U37), mixed production batches

---

## TL;DR

A 1.0 base station whose EEPROM **user** partitions hold `laser.0.current = 1` /
`laser.1.current = 1` (the factory partition still has the real values, e.g. 127 / 112)
works fine on the stock firmware, but **faults immediately on lhbs**: the LED blinks red,
the lasers never emit, and the station contributes nothing.

The cause is the laser power loop's safety window, which is anchored to the *stored*
current (`boot_current ± 60`, capped at 230). With `boot_current = 1` that window is
`[0, 61]`; a diode that needs ~127 to reach the configured 30 mW can never get there, so
the loop concludes the power sense is broken and latches `laser_fault`.

Three-line fix: treat an implausible stored current as missing.

---

## Symptom

After installing the lhbs firmware and switching to a multi-station channel (`1`–`4`, or
any channel), some units:

* blink the status LED red continuously (`P.sys_faults = 0x11`, i.e. laser fault),
* spin their rotors normally (audible, and `locked` in the console),
* never turn the lasers on, so they track nothing.

The same units work normally on the stock firmware, and they work on lhbs if the
EEPROM happens to still hold a sane current.

In a sample of six units flashed the same day with the same image, **four failed and two
worked** — so it is not a flashing error, and not random.

---

## Root cause

### The power loop caps the current at `boot_current + 60`

`laser.c`, `apc_sample()`:

```c
float target = n ? P.laser_1_pwr : P.laser_0_pwr;
uint8_t *cur = n ? &P.laser_1_current : &P.laser_0_current;
int hi = boot_current[n] + 60, lo = boot_current[n] - 60;
if (hi > 230) hi = 230;
if (target - pw > 1.0f) {                   /* too little light */
    if (*cur >= hi) {                       /* and no current headroom left */
        laser_fault = true;                 /* red blinks, lasers off */
        lasers_off();
        return;
    }
    (*cur)++;
}
```

and `laser_init()`:

```c
boot_current[0] = P.laser_0_current;        /* from EEPROM */
boot_current[1] = P.laser_1_current;
```

So the window is *relative to whatever was saved in EEPROM*, not an absolute range.

### Where the `1` comes from

On channels changes the stock firmware saves the whole parameter block. By the time it
saves, `laser.*.current` holds the value the power regulation had settled on, not the
value the unit booted with. Repeated saves therefore **ratchet** the stored current
downwards, and the factory partition keeps the real number forever.

The same ratchet is already documented in this repo (`laser_boot_currents_for_save()`,
added 2026-11-08): lhbs now saves the boot current instead of the regulated one, so it no
longer *creates* this condition. It does not *repair* units that already carry it — and
those are exactly the units that turn red.

### Why the stock firmware is fine

The stock power loop has no such ceiling: it walks the current up until the measured
power matches the target, whatever the stored value says. A stored `1` is simply a
starting point for it. lhbs starts at the same place but stops at `boot_current + 60`.

This is also the likely explanation for
[#1 "Need to flash twice"](https://github.com/YusufOzmen01/openhouse/issues/1): flashing
the stock firmware, booting it once and reflashing lhbs makes the base work — running
stock lets its power loop find the right current, and the next save writes it back.

---

## Evidence

EEPROM read back through the ISP drive: flash sector 31 (`0x1F000`) holds a
`LHEEDUMP` header followed by 4032 bytes of EEPROM at `+0x40`. Each partition is
`{"CPRM", u32 version, u32 crc32}` + a 551-byte payload; the payload is the stored
parameters concatenated in table order.

**A failing unit** (user partitions vs factory):

| parameter | user `0x000` | user `0x300` | factory `0x600` |
|---|---|---|---|
| `laser.0.current` | **1** | **1** | 127 |
| `laser.1.current` | **1** | **1** | 112 |
| `laser.0.pwr` | 30.0 | 30.0 | 30.0 |
| `laser.0.pwr.b` | 7.0851 | 7.0851 | 7.0851 |
| `laser.0.pwr.m` | 0.09106 | 0.09106 | 0.09106 |
| `laser.0.pwr.gain` | 4 | 4 | 4 |
| `rotor.0.pwm` | 2017 | 2022 | 2096 |

Only the two currents differ; every calibration constant matches the factory partition
byte for byte. All three partition CRCs validate, so this is not EEPROM corruption —
it is a well-formed save of a bad value.

A full comparison of all 183 stored parameters between the user and the factory partition
finds exactly six differences: the two currents, `timebase.mode` and `mode.current` (the
channel state), and the two auto-tuned `rotor.*.pwm` values. Nothing else is damaged.

**A working unit from the same batch of flashes**, for contrast:

| parameter | user `0x000` | user `0x300` | factory `0x600` |
|---|---|---|---|
| `laser.0.current` | 138 | 138 | 147 |
| `laser.1.current` | 125 | 125 | 141 |

Health reference from the comment in `apc_sample()`: stock default 190, healthy units
run 117–172.

---

## Fix

`basestation_firmware/src/laser.c`, in `laser_init()`, before the currents are programmed
into the ISL58303:

```diff
     /* stock laser_update_task: bias and drive current from the parameters */
+    /* Some units carry laser.0/1.current = 1 in their EEPROM user partitions: an older
+     * firmware's channel-change saves wrote the power-regulated value back, so the
+     * stored current ratcheted down while the factory partition still holds the real
+     * one (one unit here: user 1/1, factory 127/112). Stock's power loop has no ceiling
+     * and copes; ours caps at boot_current + 60, so such a base cannot reach 30 mW, is
+     * declared faulted and turns into a red-blinking station that never emits.
+     * A current below 40 cannot be a working setting (healthy units run 117-172 and the
+     * stock default is 190), so treat it as missing and use the default. The value is
+     * written back to the EEPROM by the next channel change, which heals the unit. */
+    if (P.laser_0_current < 40) P.laser_0_current = 190;
+    if (P.laser_1_current < 40) P.laser_1_current = 190;
     ok &= isl_channel_reg(isl_channel[0], 0, P.laser_0_bias);
```

With `boot_current = 190` the window becomes `[130, 230]` and the loop can find the right
current by itself. The clamp triggers only on values that cannot be working settings, so
healthy units are untouched (their stored current is ≥ 117 and the branch is not taken).

The repair is permanent: the next channel change runs
`laser_boot_currents_for_save(true)`, which saves `boot_current` — now 190 — back to
EEPROM.

### Repairing a unit that already came up red

A unit that faulted on an earlier build still carries the bad value in its EEPROM, so it
should not simply be reflashed with this build. The one verified repair, in this order:

1. **Put the unit's own stock image back**
   `Install-Firmware.bat -Restore backups\<serial>-<date>.bin`
2. **Power it on normally and let it run.** The stock power loop has no ceiling, so it
   settles the drive current on its own; setting the channel is what triggers the next
   parameter save, which writes the working value into EEPROM.
3. **Then install this build:** `Install-Firmware.bat`

This is the only repair path that has been verified. Editing the current over the console
instead leaves the unit in a state that has not been tested, so it is not recommended.

---

## Verification

Six 1.0 base stations, all of which tracked correctly on the stock firmware before
flashing, flashed the same day with the same image:

| group | stored `laser.*.current` | behaviour on 1.0.1 | after repair |
|---|---|---|---|
| 4 of 6 | below 40 (one read out directly: user `1` / `1`, factory `127` / `112`) | blinking red, no emission | **green, tracking** |
| 2 of 6 | in the healthy range | ok | not reflashed |

"After repair" means the two-step above: the unit's own stock image back, one normal
boot, then this build. The patch only takes effect when the stored current is below 40,
so the four units that changed behaviour must all have been below it.

The image was built from this tree with `arm-none-eabi-gcc 13.2.1`, vector checksum
verified (`lpcsum.py check`), written in place through the ISP drive and verified by
re-reading after a power cycle (the read matches the image byte for byte).

The boot test on a single unit was done first, on one of the already-failing stations, so
a regression could not have taken a working base out of service.

---

## Checking a unit

1. Power the station off, hold the rear button, plug the micro-USB into a PC, plug the
   power in, release. A volume labelled `CRP DISABLD` appears holding one 128 KB file.
2. In that file: `LHEEDUMP` at `0x1F000`, EEPROM at `0x1F040`.
3. Partition `n` starts at `0x1F040 + n*0x300`; skip its 12-byte header, then the payload
   is the stored parameters in table order. `sys.name` is at payload offset 4, the two
   laser currents at 98 and 124 (fixed by `params_table.h`).

A unit that is at or near `1` there will fault on any lhbs build without this patch.

---

## Related

* `#1 Need to flash twice` — probably the same root cause, seen from the other side.
* `#6` — a unit recovered by patching the current back to a factory value; this is the
  same failure, fixed in the source instead of in the binary.
