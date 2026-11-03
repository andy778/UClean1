# Ghidra decompiles — U1 (nRF9E5) and U2 (MC9S08GT32) firmware

Raw Ghidra output for both firmware images, kept here so the reasoning is
checkable against the binaries — one full decompile per chip
([`mc9s08gt32_full.c`](mc9s08gt32_full.c), [`nrf9e5_full.c`](nrf9e5_full.c)),
plus curated excerpts of the functions we relied on most. Extracted from
[`dumps/u2-mc9s08gt-flash.bin`](../../dumps/u2-mc9s08gt-flash.bin) /
[`dumps/u1-nrf9e5-8051.bin`](../../dumps/u1-nrf9e5-8051.bin) with the scripts
in [`tools/ghidra/`](../../tools/ghidra). MC9S08 addresses are the RAM/flash
addresses Ghidra shows (`file_offset + 0x107f`); nRF9E5 addresses are 8051
code-space offsets within the extracted image. Peripheral register names are
checked against the chips' own datasheets in
[`register-map.md`](register-map.md) — several nRF9E5 ones are ones Ghidra's
generic 8051 module gets wrong or can't name at all.

## Who writes the payload, who sends it

Answering directly: **U2 (MC9S08) composes and writes every payload byte; U1
(nRF9E5) never inspects it — it's a dumb relay that hands whatever arrives
straight to the nRF905 radio.** Traced end-to-end across both full decompiles:

1. U2's `FUN_db22` → `FUN_ced9` → `FUN_ce01` ([`radio_frame.c`](radio_frame.c))
   builds the 32-byte message in RAM and pushes it out over the U2↔U1 serial
   link byte-by-byte (`FUN_9fc9`/`FUN_8ec7`).
2. U1's `F_3dc` (UART RX dispatcher, [`nrf9e5_full.c`](nrf9e5_full.c)) receives
   those bytes and copies them **verbatim** into its own 32-byte ring buffer —
   no transformation, no interpretation of content.
3. U1's `FORCED_30b` (force-decompiled — outside the auto-walked call graph,
   see the header of `nrf9e5_full.c`) is where transmission actually happens:
   asserts the nRF905's chip select, sends the `W_TX_PAYLOAD` SPI opcode
   (`0x20`, confirmed against the nRF905 instruction set in the nRF9E5
   datasheet), shifts the ring-buffer bytes out, does carrier-sense collision
   backoff (up to 5 retries), then keys the transmitter and waits for the
   nRF905 to report the send complete.

So the application CRC-16, the header bytes, the node IDs, and the alarm
`[type, state]` body are all **U2's** work; U1 contributes nothing to payload
*content*, only the SPI mechanics of getting it on air.

| File | What it covers |
| --- | --- |
| [`radio_frame.c`](radio_frame.c) | Builds the 32-byte nRF905 payload + CRC (`FUN_db22` → `FUN_ced9` → `FUN_ce01`). |
| [`crc16.c`](crc16.c) | The CRC-16 update (`FUN_e0cd`/`FUN_e08e`, tables `0x8b14`/`0x8b24`). |
| [`alarm_frames.c`](alarm_frames.c) | The 5 alarm message types (`FUN_e372`/`e427`/`e0e5`). |
| [`nrf9e5_full.c`](nrf9e5_full.c) | **Full decompile, U1 (nRF9E5's embedded 8051)** — every function, incl. the actual radio TX-send routine with carrier-sense backoff. |
| [`mc9s08gt32_full.c`](mc9s08gt32_full.c) | **Full decompile, U2 (MC9S08GT32ACFBE)** — all 266 recovered functions; the 3 curated files above are excerpts of this with added line-by-line annotation. I2C EEPROM access (`FUN_a554`/`FUN_ded1`/`FUN_dfd2`/`FUN_df6a`) and peripheral init (`FUN_fe80`) are in here too, un-annotated - grep by function name (see [ghidra-firmware-analysis.md](../ghidra-firmware-analysis.md)). |
| [`register-map.md`](register-map.md) | Every peripheral register either decompile touches, checked against the chips' own datasheets (nRF9E5 PS v1.6, MC9S08GB/GT data sheet). |
| [`codes.md`](codes.md) | The 3 separate E-code/S-code/radio-type numbering spaces from the manual + firmware, kept apart (they alias). |

## What the radio payload is

The link carries the indoor **infopanel** state, not analog telemetry. Each frame
is one message with a 1-byte **type** and a boolean **state**; there are exactly
5 alarm types. Full frame = `4-byte address` + `32-byte payload`
+ `2-byte CRC`, transmitted MSB-first.

### 32-byte payload byte map (verified byte-exact, incl. a reboot capture)

| Byte | Source in firmware | Meaning |
| --- | --- | --- |
| 0 | `record[8] + record[0xb] + 12` = `12 + N` | header; `N` = message length |
| 1 | const | `0xCC` |
| 2 | const | `0x6E` |
| 3 | `((record[8]&3)<<4) \| record[9] \| 0x0f` | ctrl: bits 7–6 type (`0x40` message / `0x80` ACK), bits 5–4 extra-byte count, bits 3–0 = `1111` |
| 4–6 | RAM `0x013F–0x0141` (partner ID) | **recipient** node ID |
| 7–9 | RAM `0x010A–0x010C` (own ID) | **sender** node ID |
| 10 | `record[0xb]` | **`N` = message-body length** |
| 11 … 10+N | `&DAT_06be` | the `N` message bytes |
| 11+N … 12+N | `FUN_ce01` running CRC | **application CRC-16/CCITT-FALSE** over bytes 2 … 10+N |
| 13+N … 31 | — | **stale, not sent by U2** |

The two IDs are 4-byte values in RAM (own ID from flash `0x8000`, partner ID
from EEPROM `0x32`); the low 3 bytes go on air. An ACK swaps them. The RX side
(`FUN_caee`) accepts a frame only when the recipient equals its own ID. Earlier
versions of this page called these "correlation words" and the spec had
sender/recipient swapped; see
[`../rtl433-implementation-spec.md`](../rtl433-implementation-spec.md) §3.

### Message body — the `N` bytes at `payload[11]`

`payload[10] = N` and `hdr0 = 12 + N` cross-check. Confirmed on three frame
types from one reboot capture (pull external unit power ~10 s, plug back in):

| Frame | hdr0 | N | body | Meaning |
| --- | --- | --- | --- | --- |
| response | `0x0c` | 0 | *(none)* | ACK, no body |
| poll | `0x0d` | 1 | `24` | heartbeat opcode |
| **alarm** | `0x0e` | 2 | `20 00` | `[type=0x20 chemical_low, state=0 clear]` |

Alarm body = `[type, state]`; `state` 0/1 = clear/active. **Boot-burst capture
(2026-07-06) recorded 4 of the 5 types on air**, all `state=0`: `0x20`,
`0x21`, `0x22`, `0x23` — bodies `[type, 00]`. The 5th (`0x26`) was not seen.
An all-clear burst only shows which type numbers exist, not which alarm each
one carries; the mapping comes from the firmware (table below). See
[`alarm_frames.c`](alarm_frames.c) and [`../rtl433-decoder.md`](../rtl433-decoder.md).

**Bytes `13+N … 31` are stale, not data.** U2 sends only the `13 + N` frame
bytes (length byte through application CRC), so identical logical frames print
different tails run-to-run. Decode nothing past the application CRC.

### Two CRCs, same algorithm

Both are CRC-16/CCITT-FALSE: seed `0xFFFF` (`FUN_e088`), poly `0x1021`, no
reflection, xorout 0. See [`crc16.c`](crc16.c).

- **Application CRC**, inside the payload at `payload[11+N..12+N]`: computed by
  U2 in `FUN_ce01` over `payload[2 .. 10+N]`, checked on RX in `FUN_cc64`.
  Matches all four test vectors in the spec §6.
- **nRF905 hardware CRC**, after the payload: over `address + payload`.
  Confirmed on-air by
  [`tools/rtl433/probe_capture.py`](../../tools/rtl433/probe_capture.py) and by
  the `crc16()` gate in the rtl_433 decoder.

### The 5 message types

`FUN_db22` takes the type in X and the state in A. Ghidra's decompile drops the
X argument, which is how an earlier version of this table got the types
shifted; reading the `ldx #type` before each `jsr $db22` in `FUN_e0e5` /
`FUN_e427` gives:

| Type | Live source | Alarm code(s) | Meaning |
| --- | --- | --- | --- |
| `0x20` | `DAT_06ff` | `0x15` (E021) | chemical level low |
| `0x21` | `DAT_0707`/`070f`/`075b` (OR) | `0x1F`/`0x20`/`0x21` (E031/E032/E033) | high water |
| `0x22` | array `0x0717 + 8·i` (i = 0–5) + `DAT_0747` (OR) | `0x28–0x2D`, `0x2F` (E040–E045, E047) | device fault |
| `0x23` | `DAT_074f` | `0x33` (E051) | sludge-empty reminder |
| `0x26` | `DAT_0757` | `0x0C` (E012) | GSM fault |

E011 (`DAT_06f7`) is never sent. Codes → display E-codes are decoded in
[`../eeprom-map.md`](../eeprom-map.md).

## Status: alarm model

The `[type, state]` offset is pinned (type = `payload[11]`, state =
`payload[12]`), the rtl_433 decoder emits `msg_len`/`msg_type`/`msg_name`/
`msg_state`, and a boot-burst capture (2026-07-06, `rtl_433 ... -Y minmax`,
power pulled ~10 s) recorded 4 of the 5 types live — `0x20`/`0x21`/`0x22`/`0x23`
all `state=0`. The decoder's `msg_name` values still use the old, shifted
names (see the spec §8). `0x26` (GSM fault) was not seen in that burst. Alarm messages are **edge-driven** (`FUN_e0e5`) or fire in the boot
burst (`FUN_e372`), so a capture started *after* boot shows only heartbeats plus
the repeating status — start `rtl_433` first, then power on, to see the burst.
