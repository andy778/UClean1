# E-codes, S-codes, and the radio's own type byte — three different numbering spaces

The manual ("Åtgärder vid störningar" for faults, "PLANT STATUS" for phase)
and the firmware use **three separate, overlapping-looking numbering
schemes** for what looks like the same idea (fault/state code). Conflating
them is an easy mistake — `0x20` means something different in each one — so
this page exists to keep them apart and give each a `#define`-able name.

## 1. Radio message type (`payload[11]`, 5 values) — CONFIRMED in firmware

Meanings and live sources are the canonical table in
[`README.md`](README.md#the-5-message-types--panel-symbols); these are just
the `#define`-able names for it, already wired into the rtl_433 decoder's
`msg_type`/`msg_name` fields (`add-uponor-clean1` branch):

```c
#define UCLEAN1_MSG_STATUS          0x20  // OK / green LED
#define UCLEAN1_MSG_CHEMICAL_LOW    0x21
#define UCLEAN1_MSG_HIGH_WATER      0x22
#define UCLEAN1_MSG_SLUDGE_REMINDER 0x23
#define UCLEAN1_MSG_DEVICE_FAULT    0x26
#define UCLEAN1_MSG_HEARTBEAT       0x24  // poll opcode, not an alarm type
```

Confirmed on air (boot-burst capture 2026-07-06): `0x20`/`0x21`/`0x22`/`0x23`
all with `state=0`; only `0x26` (device_fault) still uncaptured (dropped in the
burst).

## 2. EEPROM alarm-table byte (`0x0000–0x05BF` table, 12 values) — from the dump

The 1-byte code at the start of each entry in
[`eeprom-map.md`](../eeprom-map.md)'s alarm/status message table — a
**different, finer-grained** numbering than the radio type above (e.g. radio
type `0x26` "device fault" covers *five* of these, one per failed actuator).
`#define`-able names, cross-referenced to which radio type each rolls up
into:

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
0x0063   01  1 High water           0x0289  0b   3 High water
0x007d   02  1 Pre-Aeration         0x029c   03  3 Aeration
0x0097   03  1 Aeration             0x02aa   04  3 Pump-in
0x00ac   04  1 Chemical filling     0x02bb   05  3 Waiting II
0x00c5   05  1 Dosing               0x02cf   06  3 Sludge removal
0x00dd   06  1 Mixing
0x00f7   07  1 Sedimentation I
0x0111   08  1 Sludge removal
0x012c   09  1 Sedimentation II
0x0145   0a  1 Pump-out
0x015d   0b  1 Start-up level
0x0178   0c  1 Pump-in
0x0193   0d  1 Start-up level
```

Cycle 1's 13 raw phase indices confirm the doc's existing phase-order table
([`eeprom-map.md`](../eeprom-map.md)) byte-for-byte — no reordering needed.
`High water` and `Start-up level` (both previously `?`-flagged) are now
confirmed against the board's own `J4` sensor-input silkscreen — see
[`../eeprom-map.md`](../eeprom-map.md).

**One naming nuance, not a numbering conflict:** the captured live example
(board A, [`u2-serial-protocol.md`](../u2-serial-protocol.md)) was
`DAT_0614=1, DAT_0613=2` -> `S102`. EEPROM's own label for cycle1/phase2 is
`"Pre-Aeration"`, while the manual's S-code table calls `S102` **"Aeration"**.
Same number, same formula, just the manual's plain-language gloss doesn't
distinguish "Pre-Aeration" from "Aeration" as sharply as the firmware's
internal label does — not a sign the formula or the phase order is wrong.

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

**Lead B — the firmware has a radio-triggered command path, it just can't be
traced to its source statically.** `FUN_ca4e` (`set_active_cycle`) starts a
test cycle, gated by flag `DAT_013b`. That flag has exactly two assignments in
the whole 266-function dump and **both are clears** (init + clear-after-use);
nothing statically sets it nonzero — the same computed/indirect-store pattern
as the Test-button flags (`0x014a`, `0x0150`, §5). The radio RX validator
(`FUN_caee`, aka `radio_rx_ack_validator`) exists and processes incoming
addressed frames. Put together with the field observation that the handheld
**does** start a test cycle over the air, the conclusion is: a received radio
command sets `DAT_013b` (and probably `0x014a`/`0x0150`) via a computed store,
and the main loop (`FUN_c03c`) acts on it. We can see the *effect* and the
*action* but not the *decode* of the command frame — because it's an indirect
write and because we've never captured the frame that causes it.

This also means the panel heartbeat/alarm traffic (`§1`, types `0x20-0x26`) is
only a **subset** of the radio protocol. There is a **service superset** — at
least "start test cycle" and "read status codes" — that only the handheld
exercises, with message types/bodies we have never captured. If the handheld
can read S-codes / plant status over radio, then that data *is* reachable
wirelessly (unlike the plain panel link, which never carries it) — relevant if
the wired serial/modem path is impractical (e.g. outdoors at -30 °C).

**Most useful next step:** an RF capture while the service tool is in use.
`rtl_433 -R 321 -f 868.2M -Y minmax -F json:svc.json` running throughout would
show the extra frames directly (same `EAEAEAEA`, same `mic=CRC`). Their `N`
(byte[10]) and body would reveal the command opcodes and the status-readback
format — the one piece static analysis structurally cannot recover. Replaying
those commands (to poll status ourselves) would need a **TX-capable radio**
(a cheap nRF905 module, or a TX SDR), not just the passive RTL-SDR.

*(An earlier version of this section speculated that `DAT_014a` might be this
wireless device's trigger flag - partly right in spirit, wrong in specifics.
`DAT_014a` is the styrskåp's own physical **Test button** 10-14s press,
confirmed from the manual (§5). The radio-command trigger is the *separate*
flag `DAT_013b`, above.)*

## 5. The physical Test button - CONFIRMED from the manual, partially traced in firmware

The styrskåp has a **green Test button**, wired (not wireless), documented in
the Uponor installation manual with **three different hold durations**, each
doing something different:

| Hold | Action | Display behavior |
| --- | --- | --- |
| < 5 s | Show current `PLANT STATUS` S-code | `Snnn` for 30 s, then back to *satsräknare* |
| 5–10 s | Start the **Test cycle** (EEPROM cycle 4, [eeprom-map.md](../eeprom-map.md)) | counts `1,2,3,4,S5,S6,S7…` while held, release at `S__5`; then `S400`, then the 8-step sequence |
| 10–14 s | Reset the sludge-emptying reminder | counts seconds while held; release → `E000` |

Two of the three are now tied to firmware, though neither trigger has a
traceable static setter (same class of gap as the "computed store" cases
elsewhere in this file):

- **`DAT_014a`** (RAM `0x014a`) = the **10-14s long-press** trigger. `FUN_92fb`
  (main loop) reads it and, if set, calls `FUN_93f8()` → `FUN_e372(1)` - the
  full alarm-table clear + rebroadcast (the same routine that runs at
  power-on). That's a broader reset than "just the sludge reminder," but
  functionally identical in practice: if the sludge reminder is the only
  active fault when the button is held (the manual's own use case), clearing
  everything looks the same as clearing just that one.
- The **5-10s "start Test cycle"** almost certainly runs through `FUN_ca4e`
  (`DAT_0614 = param_1` - sets the active cycle number; its one known caller
  passes an argument Ghidra's decompiler couldn't resolve statically) with
  `param_1 = 4`. Not confirmed - `FUN_ca4e`'s only visible call site is deep
  in the radio poll/ack processor (`FUN_caee`), which is an odd place for a
  button handler and needs more tracing to confirm or rule out.
- The **<5s "show status"** display behavior wasn't traced at all this pass -
  it's presumably just reading the same `DAT_0613`/`DAT_0614` this file
  already decodes and driving the display directly, without needing a radio
  message.

`FindRefsInRange.java` on `0x014a`, `0x0150`, and `0x0614` all show either no
setter or a setter whose caller passes an unresolved argument - the actual
button-read GPIO pin and hold-timer logic is still not pinned down. Given the
green Test button and its "Test btn" external header are visible on the PCB
([README.md](../../README.md)), the remaining piece is finding which GPIO
port bit reads it and the timer/counter that measures the hold duration.
