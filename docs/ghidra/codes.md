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

## 3. Manual S-codes (`S101`–`S408`) — display/SMS text, not seen as a firmware constant yet

The treatment-cycle phase codes from the manual
([`u2-serial-protocol.md`](../u2-serial-protocol.md) `PLANT STATUS:S102`).
These come back as an **ASCII string** over the serial CODE interface, not a
byte we've traced to a firmware constant table yet — unlike the two byte
codes above, we don't have a confirmed RAM offset or lookup table for "which
integer produces `S102`". `RAM 0x0613/0x0614` is the empirically-confirmed
*source* (docs/u2-serial-protocol.md), but the cycle-number -> `Sxxx` string
mapping itself hasn't been walked in Ghidra yet.

**Open item for next session:** find the string-table lookup that turns the
current cycle/phase index into `"S102"` etc. (likely near the cycle/phase
label table at EEPROM `0x0040–0x031B`, [`eeprom-map.md`](../eeprom-map.md)) -
that would let us assign real firmware offsets/values to
`UCLEAN1_S_AERATION` and friends, the same way the two tables above already
have one.
