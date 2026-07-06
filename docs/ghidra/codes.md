# E-codes, S-codes, and the radio's own type byte — three different numbering spaces

The manual ("Åtgärder vid störningar" for faults, "PLANT STATUS" for phase)
and the firmware use **three separate, overlapping-looking numbering
schemes** for what looks like the same idea (fault/state code). Conflating
them is an easy mistake — `0x20` means something different in each one — so
this page exists to keep them apart and give each a `#define`-able name.

## 1. Radio message type (`payload[11]`, 5 values) — CONFIRMED in firmware

The 5 values `FUN_e372`/`FUN_e427` send over the air, one per indoor
info-panel symbol (see [`alarm_frames.c`](alarm_frames.c),
[`README.md`](README.md)):

```c
#define UCLEAN1_MSG_STATUS          0x20  // OK / green LED
#define UCLEAN1_MSG_CHEMICAL_LOW    0x21
#define UCLEAN1_MSG_HIGH_WATER      0x22
#define UCLEAN1_MSG_SLUDGE_REMINDER 0x23
#define UCLEAN1_MSG_DEVICE_FAULT    0x26
#define UCLEAN1_MSG_HEARTBEAT       0x24  // poll opcode, not an alarm type
```

Already wired into the rtl_433 decoder's `msg_type`/`msg_name` fields
(`add-uponor-clean1` branch). Only `0x20` has been captured on air so far.

## 2. EEPROM alarm-table byte (`0x0000–0x05BF` table, 12 values) — from the dump

The 1-byte code at the start of each entry in the EEPROM's alarm/status
message table ([`eeprom-map.md`](../eeprom-map.md)). This is a **different,
finer-grained** numbering than the radio type above — e.g. radio type `0x26`
("device fault") corresponds to *five* of these EEPROM codes (one per failed
actuator):

```c
#define UCLEAN1_E_NO_RADIO_CONN     0x02  // -> E011
#define UCLEAN1_E_CHEMICAL_LOW      0x15  // -> E021  (== radio type 0x21)
#define UCLEAN1_E_HIGH_WATER_TANK   0x1F  // -> E031  (== radio type 0x22)
#define UCLEAN1_E_HIGH_WATER_OUTLET 0x20  // -> E032  (== radio type 0x22)  ** COLLIDES with radio type 0x20 (status) - not the same code space! **
#define UCLEAN1_E_COMPRESSOR_FAULT  0x28  // -> E040  (== radio type 0x26)
#define UCLEAN1_E_MV1_FAULT         0x29  // -> E041  (== radio type 0x26)
#define UCLEAN1_E_MV2_FAULT         0x2A  // -> E042  (== radio type 0x26)
#define UCLEAN1_E_MV3_FAULT         0x2B  // -> E043  (== radio type 0x26)
#define UCLEAN1_E_MV4_FAULT         0x2C  // -> E044  (== radio type 0x26)
#define UCLEAN1_E_MV5_FAULT         0x2D  // -> E045  (== radio type 0x26)
#define UCLEAN1_E_EEPROM_ERROR      0x2F  // -> E034 / E047 (pairing unconfirmed) (== radio type 0x26)
#define UCLEAN1_E_SLUDGE_REMINDER   0x33  // -> E051  (== radio type 0x23)
```

**The `0x20` collision is real and deliberate on the firmware's part, not a
copy error here**: EEPROM code `0x20` = "High water; outlet" (`E032`), but
radio type `0x20` = "status/OK". They are unrelated numbers that happen to
match — always qualify which table a bare `0x20` belongs to.

## 3. Manual S-codes (`S101`–`S408`) — SOLVED: it's arithmetic, not a lookup table

`RAM 0x0613` = phase-within-cycle, `RAM 0x0614` = cycle number (1=cleaning,
2=waiting, 3=maintenance, 4=test — capped `< 5` where it's set,
[`mc9s08gt32_full.c:5070`](mc9s08gt32_full.c)). The S-code is simply:

```c
#define UCLEAN1_SCODE(cycle, phase)  (100 * (cycle) + (phase))  // "S<cycle><phase:02>"
```

Confirmed against the raw EEPROM phase table
([`dumps/u3-m24128-eeprom.bin`](../../dumps/u3-m24128-eeprom.bin)
`0x0040–0x031B`), which stores an **explicit 1-byte phase index right before
each phase label** (not just positional order — this is a hard byte-level
check, done by reading the dump directly):

```
offset  idx  label                  offset  idx  label
0x004c  (1)  Cleaning cycle         0x027c  (1)  Waiting I    [cycle 3]
0x0063   01  1 High water?          0x0289  0b   3 High water?
0x007d   02  1 Pre-Aeration         0x029c   03  3 Aeration
0x0097   03  1 Aeration             0x02aa   04  3 Pump-in
0x00ac   04  1 Chemical filling     0x02bb   05  3 Waiting II
0x00c5   05  1 Dosing               0x02cf   06  3 Sludge removal
0x00dd   06  1 Mixing
0x00f7   07  1 Sedimentation I
0x0111   08  1 Sludge removal
0x012c   09  1 Sedimentation II
0x0145   0a  1 Pump-out
0x015d   0b  1 Start-up level?
0x0178   0c  1 Pump-in
0x0193   0d  1 Start-up level?
```

Cycle 1's 13 raw phase indices confirm the doc's existing phase-order table
([`eeprom-map.md`](../eeprom-map.md)) byte-for-byte — no reordering needed.

**One naming nuance, not a numbering conflict:** the captured live example
(board A, [`u2-serial-protocol.md`](../u2-serial-protocol.md)) was
`DAT_0614=1, DAT_0613=2` -> `S102`. EEPROM's own label for cycle1/phase2 is
`"Pre-Aeration"`, while the manual's S-code table calls `S102` **"Aeration"**.
Same number, same formula, just the manual's plain-language gloss doesn't
distinguish "Pre-Aeration" from "Aeration" as sharply as the firmware's
internal label does — not a sign the formula or the phase order is wrong.

## 4. "Service device" — not found yet, no evidence in the message-building code

Every call site that builds a radio message (`FUN_db22`/`FUN_ced9`/`FUN_cf51`,
grepped across the full [`mc9s08gt32_full.c`](mc9s08gt32_full.c)) only ever
uses the same 3 body lengths (`N`=0 ack, 1 heartbeat, 2 alarm) and the same 5
type bytes documented above — no sign of a 6th type or a longer body coming
from anywhere in U2's own radio-send code. So if there's a separate physical
service tool, it isn't feeding the 868 MHz link through code we've decompiled
so far; it would have to be a different interface entirely (the serial `CODE
RS` port? a BDM connection? something on the front panel?) — worth pinning
down what the device actually is/does before chasing this further.
