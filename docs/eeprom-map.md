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
  the message texts). This confirms the "modem port" is a GSM link. The port
  itself is the **J5 "Modem" header** (5 V TTL, on U2's SCI2 at 9600) — the
  reason the first serial tap looked dead, and how to talk to it, are in
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

### `0x03D4–0x04CF` — alarm / status message table

Each entry is a **1-byte message code** followed by the display text. These are
the states/faults the unit reports (and SMS-sends). Recovered codes:

| Code | Text                |
| ---  | ---                 |
| 0x02 | No radio connection |
| 0x15 | Chemical level low  |
| 0x1F | High water; tank    |
| 0x20 | High water; outlet  |
| 0x28 | Compressor fault    |
| 0x29 | MV1 fault           |
| 0x2A | MV2 fault           |
| 0x2B | MV3 fault           |
| 0x2C | MV4 fault           |
| 0x2D | MV5 fault           |
| 0x2F | EEPROM error        |
| 0x33 | Sludge empty remind |

(`MV1–MV5` = the five motor valves / solenoids; the phase list drives them.)

### `0x04D0–0x05BF` — actuator event / timing table

Named on/off events with big-endian `u16` timing parameters (e.g. `0x03E7` =
999, `0x0258` = 600, `0x0078` = 120): `Dosing Cnt`, `P-in-mix On`,
`P-in-mix Off`, `Pre-Aer. On`, `Pre-Aer. Off`, `Aeration On`, `Aeration Off`.
These look like the per-phase actuator schedule (which valve/pump fires, for how
long).

## What this unblocks

- **Labelling radio/telemetry:** once the 868 MHz link is captured (issue #52),
  the phase names and alarm codes above give ready-made Home Assistant state and
  fault labels.
- **Remaining unknowns:** the `0x00–0x3F` header params, the exact per-phase
  field layout, and the `0x3AD5` 2-byte record. These can be pinned down by
  cross-referencing the firmware accessors (`FUN_a554` call sites) — the next
  `stream/eeprom` task.
