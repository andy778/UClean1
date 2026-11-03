# Uponor Clean 1 radio protocol — implementation spec

A self-contained spec for implementing a decoder for this link, in any
language or framework. Written so a human or an AI agent can build a correct
decoder from this document alone, without first reading the reverse-engineering
narrative in the rest of `docs/`. Every fact here is either firmware-confirmed
(from disassembling the CPU flash) or confirmed live on air — provenance
pointers are given, not repeated, so this file doesn't drift out of sync with
the research docs. If this spec and another doc ever disagree, the research
doc with the firmware/capture citation wins; file an issue.

## 1. Physical layer

| Parameter | Value | Source |
| --- | --- | --- |
| Carrier | 868.2 MHz (channel 117) | [`rtl433-decoder.md`](rtl433-decoder.md) — `CH_NO`/`HFREQ_PLL` config bytes, matches measured carrier |
| Chip | Nordic nRF9E5 (nRF905 radio + 8051 MCU), on U1 | [`ghidra/README.md`](ghidra/README.md) |
| Line rate | 50 kbps data = 100 kchip/s on air (10 µs half-bit) | firmware config (`W_RF_CONFIG`); nRF905 fixed rate |
| Line coding | Manchester on air | **PHY effect only** — neither the 8051 nor the MC9S08 firmware does Manchester in software. Do not implement software Manchester encoding on the TX side; the nRF905 hardware does it. |
| Modulation | FSK (nRF905 ShockBurst) | nRF9E5 datasheet |

## 2. Frame format

```
[preamble] [address: EA EA EA EA, 4 bytes] [payload: 32 bytes] [CRC-16: 2 bytes]
```

- **Address**: constant `EA EA EA EA` on every unit — it's a literal in the
  8051 firmware image, not a per-unit value. Use it to anchor/sync on the
  frame, never as a device identifier (see §5).
- **CRC-16**: CCITT-FALSE — poly `0x1021`, init `0xFFFF`, no reflection, no
  xorout, big-endian trailer, computed over `address + payload` (36 bytes).
  This is the nRF905's own hardware CRC (`CRC_MODE=1`, `CRC_EN=1` in the
  radio config), not a firmware computation. **Gate every decode on this —
  reject anything that doesn't pass.**

### nRF905 register configuration (for a TX implementation)

Confirmed from the firmware's own `W_RF_CONFIG` write (not just inferred from
a capture):

```
CR0..CR9 = 75 06 44 20 20 EA EA EA EA D4
```

| Byte | Value | Meaning |
| --- | --- | --- |
| CR0 | `0x75` | `CH_NO` low 8 bits |
| CR1 | `0x06` | `CH_NO` bit 8 + `HFREQ_PLL=1` + `PA_PWR=01` (−2 dBm) — **overridden at boot to `PA_PWR=11` (+10 dBm)**, see below |
| CR2 | `0x44` | `RX_AFW=4`, `TX_AFW=4` (4-byte address width) |
| CR3 | `0x20` | `RX_PW=32` |
| CR4 | `0x20` | `TX_PW=32` |
| CR5–CR8 | `EA EA EA EA` | `RX_ADDRESS` |
| CR9 | `0xD4` | `CRC_MODE=1`, `CRC_EN=1`, `XOF=010` (16 MHz crystal) |

A separate `W_TX_ADDRESS` SPI write repeats `EA EA EA EA` as `TX_ADDRESS`.
Full trace: [`ghidra/register-map.md`](ghidra/register-map.md).

**TX power is +10 dBm in practice.** At boot U2 sends the 8051 control byte
`0x73` with parameter `03` (`0x9346: lda #3; jsr $d007`). The 8051 reads CR1
(`R_CONFIG|1`), sets `CR1 = (CR1 & 0xF3) | ((param·4) & 0x0C)` and writes it
back (`W_CONFIG|1`), so `PA_PWR` ends up `11`. Match this if a TX
implementation should have the unit's range.

## 3. Payload structure (32 bytes)

```
offset      len  field     meaning
0           1    hdr_len   12 + N (N = body length below) — counts tag … app CRC
1..2        2    tag       constant CC 6E
3           1    ctrl      bits 7–6 type (0x40 message, 0x80 ACK), bits 5–4 extra-byte count (always 0), bits 3–0 = 1111 on TX
4..6        3    node_dst  recipient's node ID
7..9        3    node_src  sender's node ID
10          1    N         message-body length (0, 1, or 2 in all observed traffic)
11..10+N         body      see §4
11+N..12+N  2    app_crc   CRC-16/CCITT-FALSE over payload[2 .. 10+N], big-endian
13+N..31         stale     not sent by U2, NOT data — see below
```

So the outer unit's messages carry ctrl `0x4F` and ACKs carry `0x8F`.

**Validation** — gate on all three:
1. The nRF905 CRC (§2).
2. `payload[0] == 12 + N`.
3. The application CRC: CRC-16/CCITT-FALSE (same parameters as §2) over
   `payload[2 .. 10+N]` (from the `6E` tag byte through the end of the body;
   the `CC` byte is not included) equals `payload[11+N..12+N]`.

U2 computes the application CRC in `FUN_ce01` (TX) and checks it in
`FUN_cc64` (RX). It matches all four test vectors in §6.

**Stale tail bytes are not data.** U2 sends only the `13 + N` frame bytes
(length byte through application CRC). The rest of the 32-byte payload is
most likely whatever the nRF905's TX payload register still held from earlier
frames. Two consequences: (a) never interpret bytes past `12+N`; (b) two
frames with identical logical content will NOT have byte-identical payloads,
because the stale tail varies run-to-run.

### Node IDs

`payload[4..6]` is the **recipient** and `payload[7..9]` the **sender**. U2's
RX filter (`FUN_caee`) accepts a frame only if the first ID equals its own ID
(RAM `0x0109`, loaded by `FUN_b69c` from flash `0x8000`) and compares the
second with the stored partner ID (RAM `0x013E`, EEPROM `0x32`). The one
exception is the identify broadcast: recipient `FF FF FF` with `body[0] == 1`.

3-byte values, stable per device, observed identical across captures days
apart:

| Device | Node ID |
| --- | --- |
| Outer (tank) unit, board B (live) | `C0 23 4B` |
| Outer (tank) unit, board A (retired) | `C0 0F 90` (flash `0x8000`) |
| Indoor panel | `80 0D 6E` (board A's stored partner, EEPROM `0x32`) |

An earlier version of this spec had the two fields and the two devices
swapped; the firmware trace and board A's stored partner ID settle it.

These are the correct per-device identifiers for a decoder's `id` field —
**not** the constant frame address (`EA EA EA EA`), which is identical on
every unit that has ever shipped and is useless for distinguishing devices.

## 4. Message body (`payload[11 .. 10+N]`)

| N | Body | Meaning |
| --- | --- | --- |
| 0 | *(none)* | ACK — used on `0x80` frames |
| 1 | `24` | heartbeat, sent to the partner every ~62 s (`FUN_dbb3`) |
| 2 | `[type, state]` | alarm. `state`: `0`=clear, `1`=active |

### Alarm `type` byte (5 values)

`FUN_db22` queues a message with the type in X and the state in A. The two
senders that map alarms to types, `FUN_e0e5` (on every change) and
`FUN_e427` (resync of all five), use this mapping. `FUN_e372` sends all five
as clear at boot.

| Type | Name | Driven by (RAM alarm record) | Display codes | On-air confirmation |
| --- | --- | --- | --- | --- |
| `0x20` | chemical_low | `0x06FF` | E021 | captured, `state=0` |
| `0x21` | high_water | `0x0707`, `0x070F`, `0x075B` (OR) | E031, E032, E033 | captured, `state=0` |
| `0x22` | device_fault | `0x0717 + 8·i` (i = 0–5), `0x0747` (OR) | E040–E045, E047 | captured, `state=0` |
| `0x23` | sludge_reminder | `0x074F` | E051 | captured, `state=0` |
| `0x26` | gsm_fault | `0x0757` | E012 | **never captured on air** |

E011 (no radio connection) is never sent, and there is no separate status/OK
type. An earlier version of this spec had these names shifted (`0x20`
status, `0x21` chemical_low, `0x22` high_water, `0x26` device_fault). The
all-clear boot burst could not tell the two mappings apart, because it only
shows which type numbers exist. The record templates settle it: `0x06FE`
holds code `0x15` (E021, `0xbe20`) and `0x0756` holds `0x0C` (E012, `0xbe93`).

No `state=1` (active-fault) frame of any type has ever been captured. Treat
the `state` field's exact bit semantics as firmware-derived, not
empirically verified, until §7's recipe is run.

Cross-reference to the EEPROM/display E-code numbering (a related but
*different* numbering space — do not conflate) is in
[`eeprom-map.md`](eeprom-map.md) and [`ghidra/codes.md`](ghidra/codes.md).

## 5. What a correct `id` field looks like

This is the one place a naive implementation gets it wrong (an earlier
version of the reference decoder did too — see the git history of
`uponor_clean1.c`). The constant frame address collides every unit on the
product line into one identifier. Use `node_src` (`payload[7..9]`) for `id`;
emit the constant address as a separate field if you want it for
debugging/framing purposes.

## 6. Test vectors

Real captured frames (boot-burst capture, 2026-07-06, power-cycling the outer
unit — full context in [`rtl433-decoder.md`](rtl433-decoder.md)). Bytes
below are `address + payload`, i.e. what a decoder sees after address-sync,
before the CRC trailer:

```
heartbeat:     eaeaeaea 0dcc6e4f800d6ec0234b0124b9c635bd3ff2f4b8abdc5effd177bff72fff6fef
chemical_low:  eaeaeaea 0ecc6e4f800d6ec0234b0220006566bd3ff2f4baabde5fbfd17fbff72fff6fef
high_water:    eaeaeaea 0ecc6e4f800d6ec0234b0221005657bd3ff2f4baabde5fbfd17fbff72fff6fef
device_fault:  eaeaeaea 0ecc6e4f800d6ec0234b0222000304bd3ff2f4baabde5fbfd17fbff72fff6fef
```

Application CRCs: `b9 c6`, `65 66`, `56 57`, `03 04`.

Decode of the `chemical_low` frame, byte by byte:

```
0e cc 6e 4f 80 0d 6e c0 23 4b 02 20 00 65 66 bd ...
|  \___/  |  \______/  \______/  |  \___/ \___/ \__________________/
|    |    |     |         |      |    |     |           `-- stale, not data
|    |    |     |         |      |    |     `-- app CRC over 6e 4f .. 20 00 = 0x6566
|    |    |     |         |      |    `-- body: type=0x20 (chemical_low), state=0x00
|    |    |     |         |      `-- N=2
|    |    |     |         `-- node_src = C0 23 4B (outer unit)
|    |    |     `-- node_dst = 80 0D 6E (panel)
|    |    `-- ctrl=0x4F -> message
|    `-- tag CC 6E
`-- hdr_len = 12+N = 0x0e
```

Expected decode for all four: `id` = the frame's `node_src` hex
(`c0234b` for all four above, since the outer unit sent all of them),
`msg_type`/`msg_name` per §4, both CRCs valid.

A reference implementation's exact output shape (JSON field names) is in the
rtl_433 decoder itself — see §8.

## 7. Recipe: capturing a `state=1` frame (`device_fault`, `0x22`)

Never captured live (§4). Rather than waiting for a real failure, force it —
the actuator self-test (`FUN_a138`/`FUN_c9e1` in the firmware, see
[`eeprom-map.md`](eeprom-map.md#how-the-firmware-addresses-the-7-output-channels--traced-from-fun_a138))
flags any output channel whose readback doesn't clear. It runs at every phase
transition, including the first one after power-up (phase 1 → 2 or 4), so a
plain power-cycle triggers it:

1. Power off the outer unit.
2. Disconnect **one** actuator from the output row's screw terminals (e.g.
   `MV5`, aeration — see [`README.md`](../README.md) for the terminal map).
3. Start capture first: `rtl_433 -R 321 -f 868.2M -Y minmax -F json:fault.json`
4. Power on — the self-test fails that channel at the phase-1 transition and
   should broadcast `msg_type=0x22`, `msg_state=1` (E045 on the display).
5. Reconnect, power-cycle again, confirm a `state=0` clear frame.

Note: the current reference decoder still uses the old type names, so it
will label this frame `high_water` until it is updated (§8).

Full recipe with rationale: [`rtl433-decoder.md`](rtl433-decoder.md).

## 8. Reference implementation

A C decoder for [rtl_433](https://github.com/merbanan/rtl_433) exists:
[andy778/rtl_433, branch `add-uponor-clean1`](https://github.com/andy778/rtl_433/blob/add-uponor-clean1/src/devices/uponor_clean1.c).
It implements address-sync via `bitbuffer_search`, all three validation gates
from §3 (nRF905 CRC, `hdr_len == 12+N`, application CRC), `id` = `node_src`
with the recipient as a separate `node_dst` field, the §4 type names, and a
`payload` field trimmed at the application CRC. It is the easiest way to see
a complete, working parser.

The §3–§4 corrections reached the decoder in
[andy778/rtl_433#5](https://github.com/andy778/rtl_433/pull/5). Earlier
versions reported the panel's ID as `id` for the outer unit's frames and used
the shifted type names, so a Home Assistant/MQTT setup built on an older
build sees the outer unit under a new `id` (`c0234b` on the live unit) after
updating. That change was verified against the §6 test vectors, not yet
against a fresh live capture.

Use `FSK_PULSE_MANCHESTER_ZEROBIT` at 10 µs bit time if implementing against
rtl_433's demodulator; see the decoder's own file header for the flex-decoder
equivalent (no C code needed) and capture recipes.

## 9. Known gaps (don't treat as spec — these are open questions, not rules)

- The radio command interface (status query, input/output readback,
  counters, pairing, test modes) has been recovered statically from the
  command dispatcher (`FUN_d07a`, jump table `0xd0da`) — see
  [#113](https://github.com/andy778/UClean1/issues/113) §3–§4 and
  [`ghidra/codes.md`](ghidra/codes.md) §4. It has never been captured or
  exercised on air. There is no authentication: write commands can stop
  the plant, drive outputs or re-pair the unit (cmd `0x80`).
- `msg_state=1` has never been observed (§4, §7).
- No TX has ever been attempted from a third-party implementation against
  the real panel — the config in §2 is correct per the firmware's own write,
  but "the panel accepts a frame built by someone else's TX chain" is
  unverified. If you build a TX implementation, that first successful
  exchange is worth documenting back into this repo.
