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
| Line rate | 100 kbps | firmware config (`W_RF_CONFIG`) |
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
| CR1 | `0x06` | `CH_NO` bit 8 + `HFREQ_PLL=1` + `PA_PWR=01` (−2 dBm) |
| CR2 | `0x44` | `RX_AFW=4`, `TX_AFW=4` (4-byte address width) |
| CR3 | `0x20` | `RX_PW=32` |
| CR4 | `0x20` | `TX_PW=32` |
| CR5–CR8 | `EA EA EA EA` | `RX_ADDRESS` |
| CR9 | `0xD4` | `CRC_MODE=1`, `CRC_EN=1`, `XOF=010` (16 MHz crystal) |

A separate `W_TX_ADDRESS` SPI write repeats `EA EA EA EA` as `TX_ADDRESS`.
Full trace: [`ghidra/register-map.md`](ghidra/register-map.md).

## 3. Payload structure (32 bytes)

```
offset  len  field         meaning
0       1    hdr_len       12 + N (N = body length below) — restates the frame length
1..2    2    tag           constant CC 6E
3       1    dir           0x40 = poll (outer unit -> panel), 0x80 = response (panel ACK)
4..6    3    node_src      sender's node ID
7..9    3    node_dst      recipient's node ID (echoed from the other side's node_src)
10      1    N             message-body length (0, 1, or 2 in all observed traffic)
11..10+N     body          see §4
11+N..31     stale         leftover TX buffer contents, NOT data — see below
```

**Validation**: `payload[0] == 12 + N` must hold. A frame that passes CRC but
fails this check is misaligned or corrupt — reject it, don't just trust CRC
alone (CRC passing only proves the 36 bytes weren't corrupted in transit, not
that your address/length parsing landed correctly).

**Stale tail bytes are not data.** `U2` (the MC9S08) fills only `11+N` bytes
of the 32-byte payload buffer and radios the rest of whatever was already
sitting in that buffer from a previous message. Two consequences for an
implementer: (a) never try to interpret bytes past `11+N`, even though they
CRC-validate and look structured; (b) two frames with identical logical
content will NOT have byte-identical payloads (only tag/dir/node/N/body match)
because the stale tail varies run-to-run.

### Node IDs

3-byte values, stable per device, observed identical across captures days
apart:

| Device | node_src (when sending) |
| --- | --- |
| Outer (tank) unit | `80 0D 6E` |
| Indoor panel | `C0 23 4B` |

These are the correct per-device identifiers for a decoder's `id` field —
**not** the constant frame address (`EA EA EA EA`), which is identical on
every unit that has ever shipped and is useless for distinguishing devices.

## 4. Message body (`payload[11 .. 10+N]`)

| N | Body | Meaning |
| --- | --- | --- |
| 0 | *(none)* | ACK — used on `response` frames |
| 1 | `24` | heartbeat (not an alarm; `0x24` is a poll opcode) |
| 2 | `[type, state]` | alarm/status. `state`: `0`=clear, `1`=active |

### Alarm/status `type` byte (5 values, = the 5 physical panel symbols)

| Type | Name | On-air confirmation |
| --- | --- | --- |
| `0x20` | status / OK (green LED) | confirmed, `state=0` |
| `0x21` | chemical_low | confirmed, `state=0` |
| `0x22` | high_water | confirmed, `state=0` |
| `0x23` | sludge_reminder | confirmed, `state=0` |
| `0x26` | device_fault | **firmware-confirmed only — never captured on air.** See §7 for how to force it. |

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
product line into one identifier. Use `node_src` for `id`; emit the constant
address as a separate field if you want it for debugging/framing purposes.

## 6. Test vectors

Real captured frames (boot-burst capture, 2026-07-06, power-cycling the outer
unit — full context in [`radio-capture-log.md`](rtl433-decoder.md)). Bytes
below are `address + payload`, i.e. what a decoder sees after address-sync,
before the CRC trailer:

```
heartbeat:  eaeaeaea 0dcc6e4f800d6ec0234b0124b9c635bd3ff2f4b8abdc5effd177bff72fff6fef
status:     eaeaeaea 0ecc6e4f800d6ec0234b0220006566bd3ff2f4baabde5fbfd17fbff72fff6fef
chem_low:   eaeaeaea 0ecc6e4f800d6ec0234b0221005657bd3ff2f4baabde5fbfd17fbff72fff6fef
high_water: eaeaeaea 0ecc6e4f800d6ec0234b0222000304bd3ff2f4baabde5fbfd17fbff72fff6fef
```

Decode of the `status` frame, byte by byte:

```
0e cc 6e 4f 80 0d 6e c0 23 4b 02 20 00 65 66 bd ...
|  \___/  |  \______/  \______/  |  \___/  \_______________________/
|    |    |     |         |      |    |             `-- stale, not data
|    |    |     |         |      |    `-- body: type=0x20, state=0x00
|    |    |     |         |      `-- N=2
|    |    |     |         `-- node_dst = C0 23 4B (panel)
|    |    |     `-- node_src = 80 0D 6E (outer unit)
|    |    `-- dir=0x40 -> poll
|    `-- tag CC 6E
`-- hdr_len = 12+N = 0x0e
```

Expected decode for all four: `id` = the frame's `node_src` hex
(`800d6e` for all four above, since the outer unit sent all of them),
`msg_type`/`msg_name` per §4, CRC valid.

A reference implementation's exact output shape (JSON field names) is in the
rtl_433 decoder itself — see §8.

## 7. Recipe: capturing `device_fault` (`0x26`) and a `state=1` frame

Never captured live (§4). Rather than waiting for a real failure, force it —
the actuator self-test (`FUN_a138`/`FUN_c9e1` in the firmware, see
[`eeprom-map.md`](eeprom-map.md#how-the-firmware-addresses-the-7-output-channels--traced-from-fun_a138))
flags any output channel whose readback doesn't clear:

1. Power off the outer unit.
2. Disconnect **one** actuator from the output row's screw terminals (e.g.
   `MV5`, aeration — see [`README.md`](../README.md) for the terminal map).
3. Start capture first: `rtl_433 -R 321 -f 868.2M -Y minmax -F json:fault.json`
4. Power on — the self-test runs at startup and should broadcast
   `msg_type=0x26`, `msg_state=1`.
5. Reconnect, power-cycle again, confirm a `state=0` clear frame.

Full recipe with rationale: [`rtl433-decoder.md`](rtl433-decoder.md).

## 8. Reference implementation

A C decoder for [rtl_433](https://github.com/merbanan/rtl_433) exists:
[andy778/rtl_433, branch `add-uponor-clean1`](https://github.com/andy778/rtl_433/blob/add-uponor-clean1/src/devices/uponor_clean1.c).
It implements every rule in this spec (address-sync via `bitbuffer_search`,
CRC gate, the `hdr_len == 12+N` validation, `id` = `node_src`) and is the
easiest way to see a complete, working parser. Use `FSK_PULSE_MANCHESTER_ZEROBIT`
at 10 µs bit time if implementing against rtl_433's demodulator; see the
decoder's own file header for the flex-decoder equivalent (no C code needed)
and capture recipes.

## 9. Known gaps (don't treat as spec — these are open questions, not rules)

- The RF service/handheld command protocol (a superset beyond §3–4) exists
  but has never been captured — see [`ghidra/codes.md`](ghidra/codes.md) §4.
- `msg_state=1` has never been observed (§4, §7).
- No TX has ever been attempted from a third-party implementation against
  the real panel — the config in §2 is correct per the firmware's own write,
  but "the panel accepts a frame built by someone else's TX chain" is
  unverified. If you build a TX implementation, that first successful
  exchange is worth documenting back into this repo.
