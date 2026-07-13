# rtl_433 decoder for the Uponor Clean 1

868.35 MHz link between inner/outer unit, decoded to MQTT for Home Assistant.
Chip: Nordic **nRF9E5** (nRF905 transceiver + 8051 MCU) on U1.

Decoder lives in the fork, not this repo:
**[andy778/rtl_433, branch `add-uponor-clean1`](https://github.com/andy778/rtl_433/blob/add-uponor-clean1/src/devices/uponor_clean1.c)**.
It is **enabled**, CRC-gated, and tested end-to-end against a real capture
(zero false positives on an unrelated 433 MHz capture).

## Capture

```bash
rtl_sdr -f 868.20M -s 1024000 -g 49 -n $((1024000*300)) capture.cu8
```

- Tune 150 kHz low (868.20M, not 868.35M) — the real carrier clears the
  RTL-SDR's DC self-mixing spike; centered on it, rtl_433 sees nothing.
- `-g 49`: high gain, low gain sits near the noise floor.
- Cadence: one event every ~62 s, a two-packet exchange (poll + response).
- Boot burst (all 5 alarm types): `rtl_433 -R 321 -f 868.2M -Y minmax -F json:boot.json`,
  then power-cycle the outer unit. `FUN_e372` blasts all states on reconnect.

Flex decoder, before the C decoder existed:

```bash
rtl_433 -f 868.20M -Y minmax -X 'n=uclean1,m=FSK_MC_ZEROBIT,s=10,r=100,bits>=200,invert'
```

`m=FSK_MC_ZEROBIT` decodes Manchester at the demodulator level (the `decode_mc`
post-filter fails — phase-offset, noisy packets). `-Y minmax` is required or
the weaker far-unit packet fragments. `invert` matches raw `01`→1 / `10`→0.

## Frame format (confirmed)

```
[preamble] [address: EA EA EA EA] [payload: 32 B] [CRC-16: 2 B]
```

Every field below is set by one write: `F_156`/`F_120`
([`radio_config_resend`/`peripheral_init`](ghidra/nrf9e5_full.c), identical
bytes at boot and on resend) puts opcode `W_RF_CONFIG` (`0x00`) + 10 config
bytes (`CR0`–`CR9`) on the nRF905's SPI bus, then a separate `W_TX_ADDRESS`
(`0x22`) write repeats the address. So the config write itself — not just a
matching capture — is the source for each row, decoded against the
config-register layout in [register-map.md](ghidra/register-map.md):

| Field | Value | Config byte(s) | Firmware detail |
| --- | --- | --- | --- |
| Carrier | 868.2 MHz (channel 117) | `CR0`=`0x75`, `CR1`=`0x06` | 9-bit `CH_NO`=`0x075`=117, `HFREQ_PLL`=1: `fRF = (422.4 + 117/10) × 2 = 868.2 MHz` — matches the measured carrier exactly. `PA_PWR`=`01` = −2 dBm output |
| Line rate | 100 kbps, Manchester-coded on air | — | capture only — neither firmware does Manchester in software, it's a PHY effect |
| Address | `EA EA EA EA`, 4 bytes | `CR2`=`0x44` (width); `CR5–8` + `W_TX_ADDRESS` = `EA EA EA EA` | `RX_AFW`=`TX_AFW`=4; the same 4 bytes are written twice — once as `RX_ADDRESS` (RX match), once as `TX_ADDRESS` (TX prefix) — confirmed on-air |
| Payload | 32 bytes | `CR3`=`0x20`, `CR4`=`0x20` | `RX_PW`=`TX_PW`=32 |
| CRC | CRC-16, poly `0x1021`, init `0xFFFF`, over address+payload | `CR9`=`0xD4` | `CRC_MODE`=1, `CRC_EN`=1 (`XOF`=`010` = 16 MHz crystal); **verified live** by `tools/rtl433/probe_capture.py` and in the decoder |

Confirmed by a ~20 h log spanning a real counter tick: both heartbeat frames
stayed byte-identical the whole span — the counter is never on air. The only
variation was a ~55 min burst matching a treatment batch running.

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
`PLANT STATUS` phase (RAM `0x0613`/`0x0614`) — neither is radio'd out. The panel
has no numeric display, so the radio only carries what the panel can *show*: the
5 alarm symbols + status. Get the counter/phase via the serial path instead:
[docs/u2-serial-protocol.md](u2-serial-protocol.md).

## What's still open

- 4 of 5 alarm types confirmed on air (`0x20`/`0x21`/`0x22`/`0x23`, boot burst
  2026-07-06, all `state=0`). Only `0x26` (device_fault) still needs catching —
  it was dropped in that burst; one more boot capture (`-Y minmax`) gets it.

## Build & test

The decoder lives on the `add-uponor-clean1` branch of
[andy778/rtl_433](https://github.com/andy778/rtl_433), a fork of upstream
[merbanan/rtl_433](https://github.com/merbanan/rtl_433) — not yet upstreamed.

```bash
git clone https://github.com/andy778/rtl_433 && cd rtl_433
git checkout add-uponor-clean1
mkdir build && cd build && cmake .. && make -j
./src/rtl_433 -r capture.cu8 -F json   # decoder is enabled, no -R needed
```

For live capture: tune ~150 kHz low (`-f 868.20M`) so the carrier clears the
RTL-SDR DC spike, `-Y minmax` (needed for the weaker far-unit packet), MQTT via
`-F "mqtt://localhost:1883,events=uclean1/events"`.
