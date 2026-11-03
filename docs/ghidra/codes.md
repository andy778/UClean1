# E-codes, S-codes, and the radio's own type byte — three different numbering spaces

The manual ("Åtgärder vid störningar" for faults, "PLANT STATUS" for phase)
and the firmware use **three separate, overlapping-looking numbering
schemes** for what looks like the same idea (fault/state code). Conflating
them is an easy mistake — `0x20` means something different in each one — so
this page exists to keep them apart and give each a `#define`-able name.

## 1. Radio message type (`payload[11]`, 5 values) — CONFIRMED in firmware

Meanings and live sources are the canonical table in
[`README.md`](README.md#the-5-message-types); these are just the
`#define`-able names for it. The rtl_433 decoder's `msg_name` field
(`add-uponor-clean1` branch) still uses an older, shifted set of names —
see the spec §8.

```c
#define UCLEAN1_MSG_CHEMICAL_LOW    0x20  // E021
#define UCLEAN1_MSG_HIGH_WATER      0x21  // E031, E032, E033
#define UCLEAN1_MSG_DEVICE_FAULT    0x22  // E040-E045, E047
#define UCLEAN1_MSG_SLUDGE_REMINDER 0x23  // E051
#define UCLEAN1_MSG_GSM_FAULT       0x26  // E012
#define UCLEAN1_MSG_HEARTBEAT       0x24  // heartbeat opcode, not an alarm type
```

Seen on air (boot-burst capture 2026-07-06): `0x20`/`0x21`/`0x22`/`0x23`
all with `state=0`; `0x26` not yet seen.

## 2. Alarm code byte (14 values) — from the dump and the RAM record templates

The 1-byte code at the start of each entry in
[`eeprom-map.md`](../eeprom-map.md)'s alarm/status message table, also
written as the template byte of each RAM alarm record (`0x06F6 + 8·i`,
`0xbe20`–`0xbe98`). A **different, finer-grained** numbering than the radio
type above (e.g. radio type `0x22` "device fault" covers seven of these).
**The display code is the decimal value of the code byte** (`0x2F` = 47 →
`E047`). `#define`-able names, cross-referenced to which radio type each
rolls up into:

```c
#define UCLEAN1_E_NO_RADIO_CONN     0x0B  // -> E011  (never sent on radio)
#define UCLEAN1_E_GSM_FAULT         0x0C  // -> E012  (== radio type 0x26)
#define UCLEAN1_E_CHEMICAL_LOW      0x15  // -> E021  (== radio type 0x20)
#define UCLEAN1_E_HIGH_WATER_TANK   0x1F  // -> E031  (== radio type 0x21)
#define UCLEAN1_E_HIGH_WATER_OUTLET 0x20  // -> E032  (== radio type 0x21)  ** COLLIDES with radio type 0x20 (chemical_low) - not the same code space! **
#define UCLEAN1_E_HIGH_WATER_PUMPST 0x21  // -> E033  (== radio type 0x21)
#define UCLEAN1_E_COMPRESSOR_FAULT  0x28  // -> E040  (== radio type 0x22)
#define UCLEAN1_E_MV1_FAULT         0x29  // -> E041  (== radio type 0x22)
#define UCLEAN1_E_MV2_FAULT         0x2A  // -> E042  (== radio type 0x22)
#define UCLEAN1_E_MV3_FAULT         0x2B  // -> E043  (== radio type 0x22)
#define UCLEAN1_E_MV4_FAULT         0x2C  // -> E044  (== radio type 0x22)
#define UCLEAN1_E_MV5_FAULT         0x2D  // -> E045  (== radio type 0x22)
#define UCLEAN1_E_EEPROM_ERROR      0x2F  // -> E047  (== radio type 0x22)
#define UCLEAN1_E_SLUDGE_REMINDER   0x33  // -> E051  (== radio type 0x23)
```

E011 was previously listed here as `0x02`; the byte before "No radio
connection" in the EEPROM table is `0x0B`, and the RAM template is written
as `0x0B` (`0xbe7f: lda #$0b; sta $06f6`). E012 and E033 are in the flash
copy of the text table but not the EEPROM one.

**The `0x20` collision is real, not a copy error here**: alarm code `0x20` =
"High water; outlet" (`E032`), but radio type `0x20` = chemical_low. They are
unrelated numbers that happen to match — always qualify which table a bare
`0x20` belongs to.

## 3. Manual S-codes (`S101`–`S408`) — a lookup table over the raw phase

`RAM 0x0613` = raw phase-within-cycle, `RAM 0x0614` = cycle number
(1=cleaning, 2=waiting, 3=keep-alive, 4=test — capped `< 5` where it's set,
[`mc9s08gt32_full.c:5070`](mc9s08gt32_full.c)). Radio cmd 3 returns these
raw values, for which `100 * cycle + phase` is a convenient label:

```c
#define UCLEAN1_RAW_SCODE(cycle, phase)  (100 * (cycle) + (phase))  // raw phase, NOT the displayed S-code
```

**The display and the status SMS do not use that formula.** `FUN_ca9c` maps
the raw phase through one table per cycle (flash `0x8885` cycle 1, `0x8905`
cycle 2, `0x8977` cycle 3; the test cycle isn't mapped), and the result
matches the manual's S-code table:

| Cycle | Raw phase → displayed S-code |
| --- | --- |
| 1 | 1–3 → `S102`, 4 → `S103`, 5 → `S104`, 6 → `S105`, 7 → `S106`, 8 → `S107`, 9 → `S108`, 10–11 → `S109`, 12–13 → `S101` |
| 2 | 1 → `S201`, 2–3 → `S202`, 4 → `S203`, 5–6 → `S204` |
| 3 | 1–2 → `S301`, 3 → `S302`, 4 → `S303`, 5 → `S304`, 6 → `S305` |

So the captured `PLANT STATUS:S102` (board A,
[`u2-serial-protocol.md`](../u2-serial-protocol.md)) means raw phase 1–3,
i.e. high-water check, pre-aeration or aeration — the manual's "Aeration".

The raw EEPROM phase table
([`dumps/u3-m24128-eeprom.bin`](../../dumps/u3-m24128-eeprom.bin)
`0x0040–0x031B`) stores an **explicit 1-byte raw phase index right before
each phase label**. Offsets below are of that index byte:

```
offset  idx  label                  offset  idx  label
0x0063   01  1 High water?          0x01cc   01  2 Waiting
0x007d   02  1 Pre-Aeration         0x01e6   02  2 Sludge removal
0x0097   03  1 Aeration             0x0200   03  2 High water?
0x00b1   04  1 Chemical filling     0x021a   04  2 Aeration
0x00cb   05  1 Dosing               0x0234   05  2 Pump-in
0x00e5   06  1 Mixing
0x00ff   07  1 Sedimentation I      0x027f   01  3 Waiting I
0x0119   08  1 Sludge removal       0x0299   02  3 High water?
0x0133   09  1 Sedimentation II     0x02b3   03  3 Aeration
0x014d   0a  1 Pump-out             0x02cd   04  3 Pump-in
0x0167   0b  1 Start-up level?      0x02e7   05  3 Waiting II
0x0181   0c  1 Pump-in              0x0301   06  3 Sludge removal
0x019b   0d  1 Start-up level?
```

The labels are literally `"High water?"` and `"Start-up level?"`, question
marks included — they are checks, not sensor names. An earlier version of this
table gave cycle 3's "High water" index as `0x0B`; that byte is part of a
`0x0BB8` duration field, and the real index is `0x02` (cycle 3's handler at
`0xc719` only accepts phases 1–6).

## 4. "Service device" — RF, no pairing, not IR: two solid leads, no capture yet

User confirms: no IR hardware on the PCB, the technician walked around a
corner while using it (so it's RF, not line-of-sight), it talks to **any**
unit with no pairing step, and — key new detail — it **started a test cycle
and read status codes out of the unit.** So it is a **bidirectional command
device**, not just a listener: it sends commands (start test) and receives
data (status). What's checked so far:

**Lead A — no pairing needed, because there's nothing to pair.** The nRF905
address `EA EA EA EA` is written as a literal in the 8051 firmware
(`nrf9e5_full.c`, `F_120`/`F_156`) — it is baked into the *image*, not read
from any per-unit serial number, EEPROM field, or config byte we've found. If
every Clean 1 unit ships with the same hardcoded address (plausible — nothing
in either decompile reads a per-unit ID into it), then a service tool that
also just hardcodes `EAEAEAEA` would trivially "talk to all devices without
pairing" — because the whole product line never had per-unit addressing to
begin with. This would need confirming from a second unit's firmware to be
certain, but it fits every observation so far without inventing a special
protocol.

**Lead B — the firmware has a full radio command interface**, recovered
statically from the dispatcher `FUN_d07a` (jump table `0xd0da`, commands
1–30 plus `0x30`–`0x32`, `0x80`, `0x81`); the full table is in
[#113](https://github.com/andy778/UClean1/issues/113) §4. A frame is accepted
when the recipient ID equals the unit's own ID — **any** sender is accepted,
no pairing needed — or when it is the identify broadcast (recipient
`FF FF FF`, cmd 1), which makes the unit reply with its ID. That explains how
the handheld talks to any unit. Relevant commands:

- cmd 3 → status: service flag, cycle, raw phase, phase timer.
- cmd 4 sub 3 → start the test cycle (the radio route to the test cycle).
- cmd 7 / cmd 8 → J4 inputs / output byte readback.
- cmd 11 `FF` → reset all alarms (sets `0x014A`, handler `0xd5d5`, write at `0xd5fc`).
- cmd `0x80` → pair: stores the sender as partner with no further checks.

There is no authentication. None of these have been captured or sent on air
yet. Checked against the dump: the jump-table entries for cmds 2, 11 and 25,
cmd 2's 1–25 index check, and that cmds 17 and 22 both point at a bare
`ais #33; rts` (`0xda50`, no action, no reply).

This means the panel heartbeat/alarm traffic (`§1`) is only a **subset** of
the radio protocol. The status data (cycle, phase) *is* reachable wirelessly
via cmd 3, which needs a **TX-capable radio** (a cheap nRF905 module, or a TX
SDR), not just the passive RTL-SDR.

*(Earlier versions of this section said nothing statically sets `DAT_013b`
and concluded a radio frame must set it. That was wrong: the Test button
handler does, at `0xa403` — see §5.)*

## 5. The physical Test button - CONFIRMED from the manual, traced in firmware

The styrskåp has a **green Test button**, wired (not wireless), documented in
the Uponor installation manual with **three different hold durations**, each
doing something different:

| Hold | Action | Display behavior |
| --- | --- | --- |
| < 5 s | Show current `PLANT STATUS` S-code | `Snnn` for 30 s, then back to *satsräknare* |
| 5–10 s | Start the **Test cycle** (EEPROM cycle 4, [eeprom-map.md](../eeprom-map.md)) | counts `1,2,3,4,S5,S6,S7…` while held, release at `S__5`; then `S400`, then the 8-step sequence |
| 10–14 s | Reset the sludge-emptying reminder | counts seconds while held; release → `E000` |

The hold logic is `FUN_a245` (thresholds from `0xa2c9`, actions in
`FUN_a339`), decided on release:

| Hold | Firmware action |
| --- | --- |
| < 5 s | show the S-code for 30 s |
| 5–10 s | start the test cycle: `0xa403: sta $013b` (flag = 1), then `lda #4; jsr $ca4e` (cycle 4). In the post-boot window the same hold disables the radio instead ("PoFF") |
| 10–15 s | zero the sludge counter, clear E051, notify the panel — works even when E051 isn't showing |
| 15–20 s | reset the GSM settings (same as SMS `RS`) |
| ≥ 20 s | cancel |

**`DAT_014a` is not the Test button.** It is written non-zero in exactly one
place, `0xd5fc`, in the radio cmd 11 handler when the parameter is `FF`; the
main loop then runs `FUN_93f8()` → `FUN_e372(1)` (clear all alarms and
rebroadcast). An earlier version of this section had `0x014A` as the 10–14 s
press.

The board has an onboard equivalent of this button too - pushbutton `S1`,
**confirmed by continuity check** ([README.md](../../README.md)) to be wired
in parallel with the `J16` "Test btn" 2-pin screw terminal (the external
button's connector) - same net, one logical input.
