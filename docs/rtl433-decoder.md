# rtl_433 decoder for the Uponor Clean 1

868.2 MHz (channel 117) link between inner/outer unit, decoded to MQTT for Home
Assistant. Chip: Nordic **nRF9E5** (nRF905 transceiver + 8051 MCU) on U1.

Decoder lives in the fork, not this repo:
**[andy778/rtl_433, branch `add-uponor-clean1-upstream`](https://github.com/andy778/rtl_433/blob/add-uponor-clean1-upstream/src/devices/uponor_clean1.c)**.
It is **enabled**, CRC-gated, and tested end-to-end against a real capture
(zero false positives on an unrelated 433 MHz capture).

Building a decoder, a TX implementation, or extending this one? Start with
**[docs/rtl433-implementation-spec.md](rtl433-implementation-spec.md)** — a
self-contained spec with test vectors, meant to be implementable without
reading the rest of this page first. This page is the narrative/provenance;
that one is the reference.

## Capture

```bash
rtl_sdr -f 868.20M -s 1024000 -g 49 -n $((1024000*300)) capture.cu8
```

- Tune 150 kHz low (868.20M, not 868.35M) — the real carrier clears the
  RTL-SDR's DC self-mixing spike; centered on it, rtl_433 sees nothing.
- `-g 49`: high gain, low gain sits near the noise floor.
- Cadence: one event every ~62 s, a two-packet exchange (poll + response).
- Boot burst (all 5 alarm types, as clear): `rtl_433 -R 321 -f 868.2M -Y minmax -F json:boot.json`,
  then power-cycle the outer unit. `FUN_e372` sends all five on reconnect.

### Receivers tested

Both dongles below decode every heartbeat/ACK exchange from indoors, using the
same command line (`rtl_433 -f 868.05M -s 1000k -Y minmax`, live, 2026-11-03).

| Dongle | Tuner | SNR (heartbeat / ACK) | FSK tones seen | Notes |
| --- | --- | --- | --- | --- |
| [RTL-SDR Blog V3](https://www.rtl-sdr.com/buy-rtl-sdr-dvb-t-dongles/) | R820T2 (reported as R820T), 1 ppm TCXO | 26–28 dB / 26–30 dB | ~868.13 / ~868.22 MHz | 6 of 6 exchanges decoded over two runs |
| Generic DVB-T USB TV stick (RTL2832U, bought from AliExpress years ago, no TCXO; looks like [this one](https://www.amazon.co.uk/dp/B0D9BVB2S2)) | R820T | ~21 dB / ~23 dB | ~868.07–868.10 / ~868.17 MHz | 1 of 1 exchange in a 75 s test |

- The tone pair the V3 sees is centred near 868.175 MHz, about 25 kHz below
  868.2 MHz. With a 1 ppm TCXO in the dongle, that offset most likely comes
  from the unit's own crystal, not the receiver.
- The TV stick reads the same signal about 50 kHz lower again (roughly
  −55 ppm relative to the V3). It still decodes; if reception gets marginal,
  `-p 55` is the starting point for correcting it.
- The TV stick has about 5 dB less SNR at the same spot, so it has less
  margin if the receiver is moved further from the outer unit.
- Both report serial `00000001`. To run both at once, give one a unique serial
  with `rtl_eeprom -s <serial>`.
- `[R82XX] PLL not locked!` at startup is harmless on both.

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
| Carrier | 868.2 MHz (channel 117) | `CR0`=`0x75`, `CR1`=`0x06` | 9-bit `CH_NO`=`0x075`=117, `HFREQ_PLL`=1: `fRF = (422.4 + 117/10) × 2 = 868.2 MHz` — matches the measured carrier exactly. `PA_PWR`=`01` = −2 dBm in this write, but U2 overrides it to `11` = **+10 dBm** at boot (control byte `0x73` + `03`, see the spec §2) |
| Line rate | 50 kbps data, Manchester-coded on air = 100 kchip/s (10 µs half-bit) | — | neither firmware does Manchester in software, it's a PHY effect |
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
[0]=12+N  [1]=CC  [2]=6E  [3]=ctrl  [4:7]=recipient  [7:10]=sender  [10]=N  [11..10+N]=body  [11+N..12+N]=app CRC  [..]=stale
```

- **Bytes `[4:7]` are the recipient's and `[7:10]` the sender's 3-byte node
  ID** (ctrl byte `0x4F` message / `0x8F` ACK; an ACK swaps the two). Stable
  per device across captures days apart: outer unit (board B) `C0 23 4B`,
  indoor panel `80 0D 6E`. An earlier version of this page had the two
  fields and the two devices swapped.
- **Byte `[10]` = `N`, the message-body length**; `[11 .. 10+N]` = the body.
  `N=0` ACK, `N=1` heartbeat (`0x24`), `N=2` alarm (`[type, state]`).
- **`[11+N .. 12+N]` is an application CRC-16/CCITT-FALSE** over
  `[2 .. 10+N]`, computed by U2. It matches every captured frame.
- **Bytes past `12+N` are stale**, not data — U2 doesn't send them, and they
  vary run-to-run for identical logical frames.

**Confirmed absent from the payload:** `CYCLE COUNTER` (RAM `0x0607`) and the
`PLANT STATUS` phase (RAM `0x0613`/`0x0614`) — neither is radio'd out. The panel
has no numeric display, so the normal panel traffic only carries the 5 alarm
types. Get the counter/phase via the serial path instead:
[docs/u2-serial-protocol.md](u2-serial-protocol.md). (The firmware also has
radio commands that return them — cmd 3 status, cmd 12 counters — recovered
statically in [#113](https://github.com/andy778/UClean1/issues/113) but never
sent; that needs a TX-capable radio.)

## What's still open

- 4 of 5 alarm types seen on air (`0x20`/`0x21`/`0x22`/`0x23`, boot burst
  2026-07-06, all `state=0`). `0x26` (GSM fault) has not been seen.
- Every capture so far is `state=0` (all-clear). No `state=1` frame has ever
  been recorded, so the state byte's meaning is firmware-derived, not observed.
  The recipe below forces one.
- The decoder in the fork uses the corrected field order and type names and
  checks the application CRC since
  [andy778/rtl_433#5](https://github.com/andy778/rtl_433/pull/5). Older
  builds reported the panel's ID as `id`, so updating changes the `id` Home
  Assistant sees for the outer unit.

### Forcing a `device_fault` (`0x22`, `state=1`) — deliberate, not by waiting

Rather than waiting for a real failure or hoping for a lucky boot burst, the
actuator self-test can be made to fail on demand. `FUN_a138` walks all 7
output channels and flags any whose readback bit never clears; `FUN_c9e1`
turns that into `E040`–`E045`, which maps to radio type `0x22` (see
[eeprom-map.md](eeprom-map.md)). It runs at every phase transition, including
the first one after power-up. So an open-circuit on one channel is exactly
the condition it looks for:

1. **Power off** the outer unit at the mains.
2. Disconnect **one** actuator from the output row's screw terminals — e.g.
   `MV5` (aeration, `E045`). One channel only, so the resulting code is
   unambiguous.
3. Start the capture *first*: `rtl_433 -R 321 -f 868.2M -Y minmax -F json:fault.json`
4. Power on. The self-test runs at the first phase transition; the fault
   should broadcast as `msg_type=0x22`, `msg_state=1` (the current decoder
   labels it `high_water`).
5. Power off, reconnect the actuator, power on, and confirm the panel returns
   to `E000` / all-clear (which should also yield a `state=0` frame — the
   clear transition, also never yet captured).

Expect this to also raise `E045` on the display and, if configured, send an
SMS — it is a real fault as far as the unit is concerned, just an induced one.
Keep it brief and don't run a treatment cycle with an actuator disconnected.

## Build & test

The decoder lives on the `add-uponor-clean1-upstream` branch of
[andy778/rtl_433](https://github.com/andy778/rtl_433), a fork of upstream
[merbanan/rtl_433](https://github.com/merbanan/rtl_433) — not yet upstreamed.

Build dependencies (Debian/Ubuntu-style packages; the fork doesn't change
upstream's requirements): `build-essential cmake librtlsdr-dev libusb-1.0-0-dev
pkg-config`.

```bash
git clone https://github.com/andy778/rtl_433 && cd rtl_433
git checkout add-uponor-clean1-upstream
mkdir build && cd build && cmake .. && make -j
./src/rtl_433 -r capture.cu8 -F json   # decoder is enabled, no -R needed
```

For live capture, use the settings this branch documents and the "Receivers
tested" table above already validated live: `-f 868.05M -s 1000k`, `-Y minmax`
(needed for the weaker far-unit packet).

```bash
./src/rtl_433 -f 868.05M -s 1000k -Y minmax -F json
```

## MQTT output

`rtl_433`'s native `-F mqtt` output publishes every decoded event as JSON to a
broker — no code changes needed, this is an upstream feature the fork
inherits as-is:

```bash
./src/rtl_433 -f 868.05M -s 1000k -Y minmax \
  -F "mqtt://localhost:1883,user=USERNAME,pass=PASSWORD,retain=0,events=uclean1/events"
```

- Default broker is `localhost:1883`; user/pass can also come from the
  `MQTT_USERNAME`/`MQTT_PASSWORD` env vars instead of the command line.
- `events=<topic>` (shown above) overrides the default event topic
  (`rtl_433/HOSTNAME/events`); drop it to use the default, or use
  `devices=uclean1[/id]` instead for one topic per device rather than one
  shared events stream.
- `mqtts://` plus `tls_cert=`/`tls_key=`/`tls_ca_cert=` enables TLS.
- The payload is this decoder's fields as JSON — `model`, `id`, `msg_type`,
  `msg_state` (see "What's still open" above for the current field names and
  known alarm-type/state mapping) — not a home-brewed schema.

**This MQTT stream is unauthenticated to anyone who can reach the broker** —
anyone able to publish/subscribe can spoof or read plant-alarm state, so keep
the broker on a trusted network/VLAN, not exposed to the internet.

## Home Assistant via MQTT discovery

Upstream rtl_433 ships `examples/rtl_433_mqtt_hass.py`, a small bridge that
subscribes to the rtl_433 events topic and publishes [Home Assistant MQTT
discovery](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery)
config messages, so decoded devices show up as HA entities automatically
instead of needing hand-written `configuration.yaml` sensors. **It's a
template, meant to be copied and edited, not run unmodified** — it only
publishes a discovery config for a field if that field's name is a key in
its `mappings` dict; anything else is silently skipped, not published as a
generic entity. The `add-uponor-clean1-upstream` branch's copy of this file
is the same as real upstream's (same `mappings` entries, `-I/--ids` filters
by `str` on both), so it has no entries for this decoder's own fields
(`msg_name`, `msg_state`, `msg_type`, `role`) — without adding them yourself,
those fields would never reach Home Assistant at all.

[`tools/rtl_433_mqtt_hass_uclean1.py`](../tools/rtl_433_mqtt_hass_uclean1.py)
in this repo is that upstream file with those four fields already mapped
(an `msg_state` binary_sensor with `device_class: problem`, an `msg_name`
sensor, and diagnostic sensors for `msg_type`/`role`) — use it as-is, or as
the base for further customization:

```bash
python3 tools/rtl_433_mqtt_hass_uclean1.py \
  -H localhost -p 1883 -u USERNAME -P PASSWORD \
  -R "uclean1/events" -D homeassistant
```

- `-R`/`--rtl-topic` must match whatever topic rtl_433's `-F mqtt` used above
  (default `rtl_433/+/events`) — if you changed `events=` on the rtl_433 side,
  pass the same value here (with the `+` wildcard if multiple hosts publish).
- `-D`/`--discovery-prefix` (default `homeassistant`) must match the
  `discovery` prefix HA's MQTT integration expects (the HA default, so usually
  leave it).
- `-i`/`--interval` controls how often configs are republished (default 600 s)
  and `-r`/`--retain` retains them so HA sees the entities again after an HA
  restart without rtl_433 needing to re-publish first.
- `msg_state` is only present on alarm frames (`msg_type` 0x20/0x21/0x22/0x23/
  0x26), not on heartbeat/ack frames — so the "Uponor Alarm Active" entity
  only appears in HA after the first real alarm frame is seen, not at startup.
- Run both processes (`rtl_433 -F mqtt://...` and this script) as
  long-running services (systemd units, or alongside HA's own add-ons) so
  discovery configs get republished and HA keeps seeing fresh state.
