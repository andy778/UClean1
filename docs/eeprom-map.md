# M24128 (U3) EEPROM — reading it and its memory map

U3 is an [M24128](https://www.st.com/en/memories/m24128-bw.html) 128-Kbit
(16 KB) serial EEPROM on the Clean 1 PCB, marked `4128BWP 8424K`, on I2C bus
address **`0x50`**. Derived from a full 16 KB dump
([`dumps/u3-m24128-eeprom.bin`](../dumps/u3-m24128-eeprom.bin), 16384 bytes).

> **Provenance:** this dump is from **board A** (the retired original, OC13
> output-driver fault), not the live unit — see the two-boards note in the
> [README](../README.md). So the config below (`PASS`, phone slots) is board
> A's, and the display counter on the live board B is **not** in this dump.

## Reading it

The M24128 is an 8-pin device (SO8/DIP8): E0/E1/E2 (address bits, pins 1–3),
VSS (4), SDA (5), SCL (6), WC (7, write control), VCC (8). Because the chip
answers at `0x50`, E0/E1/E2 are tied low — grounded **by the PCB** — so they
need no wiring from the Pi.

**What was actually connected:** four wires, top-left corner of the Pi's
40-pin header (I2C **bus 1**, pin 1 is the square pad):

| Pi header pin | Pi function  | → M24128 pin |
| ---           | ---          | ---          |
| **pin 1**     | 3V3          | 8  VCC       |
| **pin 3**     | GPIO2 / SDA1 | 5  SDA       |
| **pin 5**     | GPIO3 / SCL1 | 6  SCL       |
| **pin 6**     | GND          | 4  VSS       |

Full interactive reference: [pinout.xyz/pinout/i2c](https://pinout.xyz/pinout/i2c).
Reading a **desoldered** chip on a breadboard instead, wire E0/E1/E2 → GND
(for address `0x50`) and WC → GND or VCC yourself.

Notes:
- Bus 1's on-board ~1.8 kΩ pull-ups mean external pull-ups are usually unnecessary.
- Use **3V3, never 5V** — the Pi's GPIO is not 5V-tolerant.
- Enable I2C first: `sudo raspi-config` → Interface Options → I2C, then reboot.

**Bus contention:** reading in-circuit while the board is powered means the
MC9S08 (U2) is also an I2C master on the same bus, and early attempts showed
inconsistent results because of it. Either hold the CPU in reset while
reading, power the EEPROM alone with the rest of the board off, or desolder
U3 as a last resort.

Confirm the chip answers before dumping:

```
i2cdetect -y 1
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
50: 50 -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
```

`i2cdump` alone isn't enough: it only reaches offset `0x00–0xFF` with a single
address byte, but the M24128 needs a **14-bit (2-byte) address**, and the
firmware's live records sit near `0x3A00–0x3FFF`, well outside that reach. Use
the helper instead, which does correct 2-byte addressing:

```bash
tools/dump_eeprom.sh 1 dumps/u3-m24128-eeprom.bin
```

It prefers the kernel `at24` driver (clean binary) and falls back to chunked
`i2ctransfer`. Spot-check one known record:

```bash
i2ctransfer -y 1 w2@0x50 0x3a 0x9d r5     # 5 bytes at 0x3A9D
```

Deliverable: `dumps/u3-m24128-eeprom.bin`, exactly **16384 bytes**.

## Summary

Only two regions of the 16 KB are populated; everything else is erased
(`0xFF`) or zero:

| Range           | Contents                                            |
| ---             | ---                                                 |
| `0x0000–0x05BF` | Static program table: header params, cycle/phase definitions + labels, alarm text, actuator-event timings |
| `0x3A98–0x3AD6` | Live config: menu password + 3 GSM/SMS phone slots + trailing bytes |

**Important for Home Assistant:** there is **no live telemetry** (water level,
temperature, current phase, timers) stored in the EEPROM. Those are transient
and live in CPU RAM / are carried over the 868 MHz radio. The EEPROM holds the
*definitions and configuration*, not the running state. So the EEPROM is useful
for **decoding/labelling** what the radio carries (phase names, alarm codes),
not as a data source on its own.

## `0x3A98–0x3AD6` — live config (GSM/SMS alerting)

This region matches the firmware's typed read wrappers exactly (see the Ghidra
analysis: `FUN_a554` + 5-byte / 16-byte / 2-byte wrappers).

```
3a98: 00 00 00 08 01                                  header/flags
3a9d: 50 41 53 53 00                "PASS\0"           menu password (5-byte record)
3aa2: 01 00 00                                         flags/count
3aa5: 2b 33 35 38 30 30 30 30 30 30 30 30 30  "+358000000000"  phone slot 0  (16-byte stride)
3ab5: 2b 33 35 38 30 30 30 30 30 30 30 30 30  "+358000000000"  phone slot 1
3ac5: 2b 33 35 38 30 30 30 30 30 30 30 30 30  "+358000000000"  phone slot 2
3ad5: 70 64                                            2-byte record (unknown; setting/checksum)
```

- **Password** `PASS` and all three **phone numbers** (`+358…`, Finland) are
  factory **placeholders / unconfigured** — no real credentials in this dump.
- The unit sends **SMS alerts** to these numbers (the alarm strings below are
  the message texts). This confirms the "modem port" is a GSM link — the
  **J5 "Modem" header**, on U2's SCI2 at 9600 (RS-232 via the SP3232 line
  driver, not raw TTL). Wiring and how to talk to it:
  [docs/u2-serial-protocol.md](u2-serial-protocol.md).

## `0x0000–0x05BF` — static program table

### `0x0000–0x003F` — header / global parameters

A block of big-endian `u16` values (counts, setpoints, durations) that the
firmware reads at boot. Not fully decoded; examples: `0x0851`, `0x039F`,
`0x0320`, `0x0D6E`, `0x0574`. Treat as configuration constants for now.

### `0x0040–0x031B` — cycle & phase definitions

Four operating **cycles**, each a header label followed by its ordered
**phases**. Every phase entry carries numeric fields (probable durations in
seconds, big-endian `u16` — e.g. `0x0258` = 600 = 10 min, `0x0BB8` = 3000) plus
a phase-index byte, then a label of the form `"<cycle#> <PhaseName>"`.

| Cycle | Label            | Phases (in order) |
| ---   | ---              | --- |
| 1     | Cleaning cycle   | High water, Pre-Aeration, Aeration, Chemical filling, Dosing, Mixing, Sedimentation I, Sludge removal, Sedimentation II, Pump-out, Start-up level, Pump-in, Start-up level |
| 2     | Waiting cycle    | Waiting, Sludge removal, High water, Aeration, Pump-in, 3 days? |
| 3     | Keep-alive cycle | Waiting I, High water, Aeration, Pump-in, Waiting II, Sludge removal |
| 4     | Test cycle       | Pump-in, Sludge removal, Pump-out, Chemical filling, Dosing, Aeration |

`High water` and `Start-up level` (previously flagged `?`, uncertain OCR/reading
of the EEPROM phase-name bytes) are now **confirmed, not guessed**: they're the
exact silkscreen names of two of the board's 4 sensor inputs (`J4` header —
`Startup level`, `High water`, `Chemical pressure`, `Spare`; see
[README.md](../README.md)).

**Cycle 4 (`Test cycle`), full 8-phase list + real durations — from the manual**
(installation manual, "Test function" section; matches this EEPROM entry's 6
named phases plus the two unnamed steps the durations-only reading missed):

| S-code | Step | Duration |
| --- | --- | --- |
| `S401` | Pump-in | 20 s |
| `S402` | Sludge removal | 20 s |
| `S403` | Pump-out | 5 s |
| `S404` | Chemical-pump fill | 90 s |
| `S405` | Chemical dosing | 10 s |
| `S406` | Precipitation/settling (no actuator on) | 10 s |
| `S407` | Aeration I | 30 s |
| `S408` | Aeration II | 30 s |

Triggered by the styrskåp's own **green Test button** (not radio/serial): hold
**5–10 seconds** (release when the display reaches `S__5`, i.e. `S405`) starts
it; the display counts `1,2,3,4,S5,S6,S7…` while held, then shows `S400`, then
runs the 8 steps above in order, then returns to the *satsräknare* (the cycle
counter shown on the display in normal operation). Also reachable via the
PlantCare app (`Test > Test cycle`).

**Two other Test-button hold durations, same button:**
- **< 5 seconds**: shows the current `PLANT STATUS` S-code (RAM
  `0x0613`/`0x0614`, formula below) on the display for 30 seconds, then
  reverts to the *satsräknare*.
- **10–14 seconds**: resets the sludge-emptying reminder (only meaningful
  while that reminder's fault code, `E051`/EEPROM code `0x33`, is showing).
  Release and the display shows `E000` (no fault).

Firmware-side, the long-press reset maps to `RAM 0x014a` (checked in the main
loop `FUN_92fb`; when set, `FUN_93f8()` fires `FUN_e372(1)`, the same full
alarm-table clear + rebroadcast that runs at power-on). The 5–10s test-cycle
start likely runs through `FUN_ca4e` (sets the active cycle number), though
that's less certain. Neither trigger's setter has been traced back to an
actual GPIO read/hold-timer yet — see
[docs/ghidra/codes.md](ghidra/codes.md) §5 for what's confirmed vs. still
open.

**This phase-index byte is the manual's `S`-code, directly.** Confirmed in
firmware (RAM `0x0613`=phase, `0x0614`=cycle,
[`docs/ghidra/codes.md`](ghidra/codes.md)): `Snnn = 100*cycle + phase-index`,
where `phase-index` is exactly this byte. E.g. cycle 1 phase-index 2
("Pre-Aeration" above) = `S102`, the captured live example
([`u2-serial-protocol.md`](u2-serial-protocol.md)).

### `0x03D4–0x04CF` — alarm / status message table

Each entry is a **1-byte message code** followed by the display text. These are
the states/faults the unit reports (and SMS-sends). The **Display** column is the
`E`-code the styrskåp shows for the same fault, from the Uponor Clean I manual
("Åtgärder vid störningar") — so byte / SMS text / display code are three views of
one fault (see [u2-serial-protocol.md](u2-serial-protocol.md), `ALARM STATUS`):

| Code | Text                | Display | Valve / cause |
| ---  | ---                 | ---     | --- |
| 0x02 | No radio connection | `E011`  | control-panel link lost |
| 0x15 | Chemical level low  | `E021`  | low flocculant |
| 0x1F | High water; tank    | `E031`  | inlet module blocked |
| 0x20 | High water; outlet  | `E032`  | outlet / pump-out blocked |
| 0x28 | Compressor fault    | `E040`  | air pump |
| 0x29 | MV1 fault           | `E041`  | chemical-dosing valve |
| 0x2A | MV2 fault           | `E042`  | sludge-return valve |
| 0x2B | MV3 fault           | `E043`  | pump-out valve |
| 0x2C | MV4 fault           | `E044`  | pump-in valve |
| 0x2D | MV5 fault           | `E045`  | aeration valve |
| 0x2F | EEPROM error        | `E034`/`E047` | control-cabinet / program fault (pairing unconfirmed) |
| 0x33 | Sludge empty remind | `E051`  | septic section filling |

So `MV1–MV5` are the five solenoid valves — **dosing, sludge-return, pump-out,
pump-in, aeration** — which the phase list drives. (`E401`–`E403` are the 1/3/6-year
service reminders; `E000` = no fault.) **Confirmed by silkscreen**: the output
row's own screw terminals are labelled `MV1 Compressor`, `MV2`–`MV5`, `Spare`
([README](../README.md)) — matching this EEPROM-derived naming exactly.

### How the firmware addresses the 7 output channels — traced from `FUN_a138`

The "seven identical output-driver channels" (README) are not individually
wired GPIOs — they're **multiplexed off one shared address bus on `PTBD`**.
`FUN_a138` (`actuator_self_test`, [`ghidra/mc9s08gt32_full.c`](ghidra/mc9s08gt32_full.c))
walks all 7 channels using a lookup table at flash `0x80B3`:

```
DAT_80b3 = 00 02 04 06 08 0a 0c   (channel_index * 2)
```

For each channel it writes `PTBD = DAT_80b3[i] | 0x10 | (PTBD & 0xc0)` (address +
strobe bit), waits, then samples a readback bit (`PTBD & 1`) up to 30 times — a
channel that never clears that bit is flagged failed. The 7-bit pass/fail
result is consumed by `FUN_c9e1` (`actuator_self_test_and_report`), which turns
a failing bit `i` (0–5 only; bit 6/Spare is excluded) directly into an alarm
code:

```
alarm_code = channel_index + 0x28
```

...which is exactly the `E040`–`E045` sequence above. This **confirms the
firmware's channel order matches the silkscreen order 1:1**, and resolves the
"`MV1 Compressor`" combined-looking silkscreen label: they're genuinely two
separate adjacent channels (index 0 and 1), not one terminal.

| Channel index | Mux code | Alarm code | Function |
| --- | --- | --- | --- |
| 0 | `0x00` | `E040` (`0x28`) | Compressor |
| 1 | `0x02` | `E041` (`0x29`) | MV1 — chemical-dosing valve |
| 2 | `0x04` | `E042` (`0x2A`) | MV2 — sludge-return valve |
| 3 | `0x06` | `E043` (`0x2B`) | MV3 — pump-out valve |
| 4 | `0x08` | `E044` (`0x2C`) | MV4 — pump-in valve |
| 5 | `0x0a` | `E045` (`0x2D`) | MV5 — aeration valve |
| 6 | `0x0c` | *(not checked)* | Spare — confirms it's genuinely untested/unused |

Each fault also writes into a per-channel status array at `0x0717 + i*8`
(`0x717, 0x71F, 0x727, 0x72F, 0x737, 0x73F`) — the same array the radio
`device_fault` message (`0x26`) draws from
([ghidra/README.md](ghidra/README.md)). `FUN_a0da`
(`mux_read_sensor_channels`) reads the same `PTBD` bus for channels 0–3
(likely the analog/level sensor side of the mux, not yet identified further)
on a periodic background task, mutually exclusive with the self-test via
`DAT_05f8`.

**Still open:** exactly when `FUN_c9e1(2)` (the self-test entry point) fires —
it's called from a phase-transition branch of the main state machine, not yet
pinned to "every boot" vs. "every cycle start" — and `FUN_b6cd` (called on a
failing channel, presumably the alarm-broadcast trigger) hasn't been traced.

### `0x04D0–0x05BF` — actuator event / timing table

Named on/off events with big-endian `u16` timing parameters (e.g. `0x03E7` =
999, `0x0258` = 600, `0x0078` = 120): `Dosing Cnt`, `P-in-mix On`,
`P-in-mix Off`, `Pre-Aer. On`, `Pre-Aer. Off`, `Aeration On`, `Aeration Off`.
These look like the per-phase actuator schedule (which valve/pump fires, for how
long).

## What this unblocks

- **Labelling radio/telemetry:** the phase names and alarm codes above are the
  ready-made Home Assistant state/fault labels once the corresponding radio or
  serial byte offset is identified (see [rtl433-decoder.md](rtl433-decoder.md)
  and [u2-serial-protocol.md](u2-serial-protocol.md)).
- **Remaining unknowns:** the `0x00–0x3F` header params, the exact per-phase
  field layout, and the `0x3AD5` 2-byte record.
