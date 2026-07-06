# M24128 (U3) EEPROM memory map

Derived from a full 16 KB dump ([`dumps/u3-m24128-eeprom.bin`](../dumps/u3-m24128-eeprom.bin),
16384 bytes) — see [docs/u3-eeprom.md](u3-eeprom.md) for how it was read.

> **Provenance:** this dump is from **board A** (the retired original, OC13
> output-driver fault), not the live unit — see the two-boards note in the
> [README](../README.md). So the config below (`PASS`, phone slots) is board A's,
> and the display counter on the live board B is **not** in this dump.

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
| 1     | Cleaning cycle   | High water?, Pre-Aeration, Aeration, Chemical filling, Dosing, Mixing, Sedimentation I, Sludge removal, Sedimentation II, Pump-out, Start-up level?, Pump-in, Start-up level? |
| 2     | Waiting cycle    | Waiting, Sludge removal, High water?, Aeration, Pump-in, 3 days? |
| 3     | Keep-alive cycle | Waiting I, High water?, Aeration, Pump-in, Waiting II, Sludge removal |
| 4     | Test cycle       | Pump-in, Sludge removal, Pump-out, Chemical filling, Dosing, Aeration |

**Cycle 4 (`Test cycle`), full 8-phase list + real durations — from the manual**
(installation manual, "Testfunktion"; matches this EEPROM entry's 6 named
phases plus the two unnamed steps the durations-only reading missed):

| S-code | Function (sv) | Meaning | Duration |
| --- | --- | --- | --- |
| `S401` | Inpumpning | Pump-in | 20 s |
| `S402` | Slamtömning | Sludge removal | 20 s |
| `S403` | Utpumpning | Pump-out | 5 s |
| `S404` | Påfyllning av kemikaliepumpen | Chemical-pump fill | 90 s |
| `S405` | Dosering av kemikalie | Chemical dosing | 10 s |
| `S406` | Fällning (inga funktioner aktiva) | Precipitation/settling (no actuator on) | 10 s |
| `S407` | Luftning I | Aeration I | 30 s |
| `S408` | Luftning II | Aeration II | 30 s |

Triggered by the styrskåp's own **green Test button** (not radio/serial): hold
**5–10 seconds** (release when the display reaches `S__5`, i.e. `S405`) starts
it; the display counts `1,2,3,4,S5,S6,S7…` while held, then shows `S400`, then
runs the 8 steps above in order, then returns to the *satsräknare*. Also
reachable via the PlantCare app (`Test > testcykel`).

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
