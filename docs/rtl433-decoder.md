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

## Payload structure

The full byte map lives in **[docs/ghidra/README.md](ghidra/README.md)** (the
canonical source, derived from the MC9S08 serializer `FUN_ce01`). In short, the
32-byte payload is a **messaging protocol**, not telemetry:

```
[0]=12+N  [1]=CC  [2]=6E  [3]=flags|dir  [4:10]=two node-IDs  [10]=N  [11..]=body  [..]=stale
```

- **Bytes `[4:10]` are two 3-byte node IDs**, a poll/ack cookie — each side
  sends "my ID" and echoes "your ID" (`dir` byte `0x40` poll / `0x80` response).
  Stable per device across captures days apart: outer unit `80 0D 6E`,
  infopanel `C0 23 4B`.
- **Byte `[10]` = `N`, the message-body length**; `[11 .. 10+N]` = the body.
  `N=0` ACK, `N=1` heartbeat (`0x24`), `N=2` alarm (`[type, state]`).
- **Bytes past `10+N` are stale buffer**, not data — they vary run-to-run for
  identical logical frames (CRC still passes; U2 CRCs whatever it sends).

**Confirmed absent from the payload:** `CYCLE COUNTER` (RAM `0x0607`) and the
`PLANT STATUS` phase (RAM `0x0613`/`0x0614`) — neither is radio'd out (a 20 h
capture over a real counter tick showed byte-identical heartbeats). The panel
has no numeric display, so the radio only carries what the panel can *show*: the
5 alarm symbols + status. Get the counter/phase via the serial path instead:
[docs/u2-serial-protocol.md](u2-serial-protocol.md).

## What's still open

- 4 of 5 alarm types confirmed on air (`0x20`/`0x21`/`0x22`/`0x23`, boot burst
  2026-07-06, all `state=0`). Only `0x26` (device_fault) still needs catching —
  it was dropped in that burst; one more boot capture (`-Y minmax`) gets it.

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
