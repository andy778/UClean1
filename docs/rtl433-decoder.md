# rtl_433 decoder for the Uponor Clean 1

868.35 MHz link between inner/outer unit, decoded to MQTT for Home Assistant.
Chip: Nordic **nRF9E5** (nRF905 transceiver + 8051 MCU) on U1.

Decoder lives in the fork, not this repo:
**[andy778/rtl_433, branch `add-uponor-clean1`](https://github.com/andy778/rtl_433/blob/add-uponor-clean1/src/devices/uponor_clean1.c)**.
It is **enabled**, CRC-gated, and tested end-to-end against a real capture
(zero false positives on an unrelated 433 MHz capture).

## Frame format (confirmed)

```
[preamble] [address: EA EA EA EA] [payload: 32 B] [CRC-16: 2 B]
```

| Param | Value | Source |
| --- | --- | --- |
| Carrier | 868.2 MHz (channel 117) | firmware `W_CONFIG`, matches measured carrier |
| Line rate | 100 kbps, Manchester-coded on air | capture (neither firmware does Manchester in software — it's a PHY effect) |
| Address | `EA EA EA EA`, 4 bytes | firmware `W_CONFIG` / `W_TX_ADDRESS`; confirmed on-air |
| Payload | 32 bytes | firmware `RX_PW`/`TX_PW` |
| CRC | CRC-16, poly `0x1021`, init `0xFFFF`, over address+payload | firmware `CRC_MODE`; **verified live** by `tools/rtl433/probe_capture.py` and in the decoder |

How this was found: [docs/nrf9e5-firmware.md](nrf9e5-firmware.md) (firmware) and
[docs/radio-capture-log.md](radio-capture-log.md) (captures).

## Payload sub-fields (partially mapped)

Reading the MC9S08 frame serializer (`FUN_ce01`) gives an exact byte formula,
verified against two real CRC-passing packets:

| Offset | Field | Meaning |
| --- | --- | --- |
| `[0]` | `hdr0` | `record[8]+record[0xb]+12` — a length/sub-type byte |
| `[3]` | `hdr3` | candidate type/direction marker (`0x40` vs `0x80` seen) |
| `[4]` | `word_013e_lo` | MC9S08 RAM `0x013e`, low byte only (high byte never sent) |
| `[5:7]` | `word_0140` | MC9S08 RAM `0x0140`, full 16-bit |
| `[7]` | `word_0109_lo` | MC9S08 RAM `0x0109`, low byte only |
| `[8:10]` | `word_010b` | MC9S08 RAM `0x010b`, full 16-bit |
| `[10:32]` | — | **not mapped** |

These four RAM words behave like a poll/ack correlation cookie, not sensor
telemetry: each side sends "my `(013e,0140)`" and echoes "your `(0109,010b)`" —
confirmed live (packet A's `word_013e_lo/word_0140` == packet B's
`word_0109_lo/word_010b`, and vice versa).

**Confirmed absent from the payload:** `CYCLE COUNTER` (RAM `0x0607`) and the
`PLANT STATUS` source (RAM `0x0613`/`0x0614`) — neither is radio'd out. Get
those via the serial path instead: [docs/u2-serial-protocol.md](u2-serial-protocol.md).

## What's still open

- Payload bytes `[10:32)` — traced to a count-loop with computed/indirect
  addressing in the firmware, not resolved statically.
- Physical meaning of `word_0140`/`word_010b` and the `hdr3` marker.
- Resolved by either: more firmware tracing, or a **display-correlated
  capture** (change the level / force an alarm, diff the payload).

The alarm code table ([eeprom-map.md](eeprom-map.md)) and phase/status S-codes
([u2-serial-protocol.md](u2-serial-protocol.md)) are the decode targets once a
byte offset is found for them.

## Build & test

```bash
git clone https://github.com/andy778/rtl_433 && cd rtl_433
git checkout add-uponor-clean1
mkdir build && cd build && cmake .. && make -j
./src/rtl_433 -r capture.cu8 -F json   # decoder is enabled, no -R needed
```

For live capture: tune ~150 kHz low (`-f 868.20M`) so the carrier clears the
RTL-SDR DC spike, `-Y minmax` (needed for the weaker far-unit packet), MQTT via
`-F "mqtt://localhost:1883,events=uclean1/events"`.
