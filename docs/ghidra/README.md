# Ghidra decompiles — U2 firmware

Raw Ghidra output for the U2 (MC9S08GT) functions we relied on, kept here so the
reasoning is checkable against the binary. Extracted from
[`dumps/u2-mc9s08gt-flash.bin`](../../dumps/u2-mc9s08gt-flash.bin) with the
scripts in [`tools/ghidra/`](../../tools/ghidra). Addresses are the RAM/flash
addresses Ghidra shows (`file_offset + 0x107f`).

| File | What it covers |
| --- | --- |
| [`radio_frame.c`](radio_frame.c) | Builds the 32-byte nRF905 payload + CRC (`FUN_db22` → `FUN_ced9` → `FUN_ce01`). |
| [`crc16.c`](crc16.c) | The CRC-16 update (`FUN_e0cd`/`FUN_e08e`, tables `0x8b14`/`0x8b24`). |
| [`alarm_frames.c`](alarm_frames.c) | The 5 alarm/status message types → the 5 infopanel symbols (`FUN_e372`/`e427`/`e0e5`). |
| [`eeprom_i2c.c`](eeprom_i2c.c), [`periph_init.c`](periph_init.c) | I2C EEPROM access and peripheral bring-up. |

## What the radio payload is

The link carries the indoor **infopanel** state, not analog telemetry. Each frame
is one message with a 1-byte **type** and a boolean **state**; there are exactly
5 types, one per panel symbol. Full frame = `4-byte address` + `32-byte payload`
+ `2-byte CRC`, transmitted MSB-first.

### 32-byte payload byte map (verified byte-exact, incl. a reboot capture)

| Byte | Source in firmware | Meaning |
| --- | --- | --- |
| 0 | `record[8] + record[0xb] + 12` = `12 + N` | header; `N` = message length |
| 1 | const | `0xCC` |
| 2 | const | `0x6E` |
| 3 | `((record[8]&3)<<4) \| record[9] \| 0x0f` | flags + **direction** (`record[9]`: `0x40` poll / `0x80` response) |
| 4 | RAM `0x013e` lo | correlation word 1 |
| 5–6 | RAM `0x0140` | correlation word 2 |
| 7 | RAM `0x0109` lo | correlation word 3 |
| 8–9 | RAM `0x010b` | correlation word 4 |
| 10 | `record[0xb]` | **`N` = message-body length** |
| 11 … 10+N | `&DAT_06be` | the `N` message bytes |
| 11+N … 31 | — | **stale buffer, not data** |

The four correlation words are a poll/ack cookie: the **response** frame carries
the same fields with the direction bit set to `0x80` and the ID pairs swapped
(an ACK handshake). See [`radio_frame.c`](radio_frame.c) `FUN_db22`.

### Message body — the `N` bytes at `payload[11]`

`payload[10] = N` and `hdr0 = 12 + N` cross-check. Confirmed on three frame
types from one reboot capture (pull external unit power ~10 s, plug back in):

| Frame | hdr0 | N | body | Meaning |
| --- | --- | --- | --- | --- |
| response | `0x0c` | 0 | *(none)* | ACK, no body |
| poll | `0x0d` | 1 | `24` | heartbeat opcode |
| **alarm** | `0x0e` | 2 | `20 00` | `[type=0x20 status, state=0 OK]` |

Alarm body = `[type, state]`; `state` 0/1 = clear/active. Only type `0x20`
(status / green LED idle) has been captured so far — the other four
(`0x21`/`0x22`/`0x23`/`0x26`) need a capture **started before power-on** to catch
the boot burst. See [`alarm_frames.c`](alarm_frames.c).

**Bytes `11+N … 31` are stale buffer, not data.** U2 fills only `N` body bytes
and radios the leftover buffer, so identical logical frames print different
tails run-to-run — the CRC still passes because U2 computes it over whatever it
sends. Decode nothing past `payload[10] + N`.

### Trailer = CRC-16/CCITT-FALSE

Seed `0xFFFF` (`FUN_e088`), poly `0x1021`, no reflection, xorout 0, over
`address + payload`. Confirmed on-air by
[`tools/rtl433/probe_capture.py`](../../tools/rtl433/probe_capture.py) and by the
`crc16()` gate in the rtl_433 decoder. See [`crc16.c`](crc16.c).

### The 5 message types → panel symbols

| Type | Live source | Alarm code(s) | Panel symbol |
| --- | --- | --- | --- |
| `0x20` | `DAT_0757` | `0x02` (E011) | status / OK LED |
| `0x21` | `DAT_06ff` | `0x15` (E021) | chemical level low |
| `0x22` | `DAT_0707/070f/075b` + array | `0x1F`/`0x20` (E03x) | high water |
| `0x23` | `DAT_074f` | `0x33` (E051) | sludge-empty reminder |
| `0x26` | `DAT_0747` + array `0x0717..` | `0x2F`, `0x28–0x2D` | device fault `!` |

Codes → display E-codes are decoded in [`../eeprom-map.md`](../eeprom-map.md).

## Open item for the next capture

The `[type, state]` offset is now pinned (type = `payload[11]`, state =
`payload[12]`), and the rtl_433 decoder emits `msg_len`/`msg_type`/`msg_name`/
`msg_state`. Only the `0x20` (status) type has been seen on air so far.

Alarm messages are **edge-driven** (`FUN_e0e5`) or fire in the boot burst
(`FUN_e372`), so a capture started *after* boot shows only the heartbeat frames
plus the repeating status. To catch all five types (`0x20`/`0x21`/`0x22`/`0x23`/
`0x26`), **start `rtl_433` first, then power the unit on** — that confirms the
`msg_name` mapping for the remaining four symbols (chemical_low / high_water /
sludge_reminder / device_fault). See [`../rtl433-decoder.md`](../rtl433-decoder.md).
