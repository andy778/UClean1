# Radio capture notes — Uponor Clean 1 (868.35 MHz)

Bench recipes and capture history behind [docs/rtl433-decoder.md](rtl433-decoder.md).
Current confirmed facts live there; this page is provenance + how-to-capture.

## Capturing

```bash
rtl_sdr -f 868.20M -s 1024000 -g 49 -n $((1024000*300)) capture.cu8
```

- `-f 868.20M`: tune ~150 kHz low so the real carrier (~868.2 MHz) clears the
  RTL-SDR's DC self-mixing spike (an early capture centered on 868.35M landed
  right on it and rtl_433 saw nothing).
- `-g 49`: high gain — a low-gain capture sat near the noise floor.
- Or let rtl_433 save each event: `rtl_433 -f 868.20M -s 1024k -g 49 -S all -Y minmax -Y minlevel=-40 -Y minsnr=3`.

Cadence: one radio event every ~62 s, each a two-packet exchange (~6.2 ms poll,
~19 ms gap, ~6.6 ms response) — the two units answering each other.

## Flex-decoder pass (before building the C decoder)

```bash
rtl_433 -f 868.20M -Y minmax -X 'n=uclean1,m=FSK_MC_ZEROBIT,s=10,r=100,bits>=200,invert'
```

- `m=FSK_MC_ZEROBIT`: demodulator-level Manchester slicer — decodes from pulse
  timing, so output is already-decoded bytes. (The `decode_mc` post-filter does
  **not** work here — it aborts at the first Manchester violation and only
  tries phase 0; these packets are phase-offset and noisy.)
- `-Y minmax`: required — the default detector fragments the weaker far-unit
  packet.
- `invert`: matches the raw `01`→1 / `10`→0 convention.

Raw (still Manchester-encoded) bits, for eyeballing: `-X 'n=uclean1,m=FSK_PCM,s=10,l=10,r=100,bits>=400'`.

## Standalone demod (no rtl_433, numpy)

For working directly from `.cu8` (used to build `tools/rtl433/probe_capture.py`):
FM-discriminate (`np.angle(iq[1:] * np.conj(iq[:-1]))`), find bursts by
magnitude, resample at ~10.0 samples/chip (scan start phase + rate), hard-slice
on sign, Manchester-decode both bit-parities, search for `EA EA EA EA`.

## What idle captures showed (before the frame model was confirmed)

A 1.5 h idle log (730 frames) found:

- The **response frame is 100% static**; all variation is in the poll stream.
- The idle poll is a **doubled 13-byte record** (same record sent twice, plus
  flag bytes and the trailer) — record layout: magic · status byte (idle/active)
  · 4-byte field · 3-byte field · const · 2-byte tail.
- A pair of 3-byte fields swap between poll and response (the node-ID-like
  correlation cookie, since confirmed at exact byte offsets — see the decoder doc).

A ~20 h log spanning a real counter tick (1257→1258, `uclean1-20260702-2128.json`)
showed both heartbeat frames byte-identical across the whole span — confirming
the counter is never on air. The only variation was a ~55 min burst (the
"active record" pattern above) matching a treatment batch running; the counter
advances when it completes. So the radio gives a "treating" event, not the
numeric counter — get that from the serial path
([u2-serial-protocol.md](u2-serial-protocol.md)).

## Analyzing in Universal Radio Hacker (URH), if starting from scratch

1. Set sample rate to match the capture, demodulate as FSK.
2. Read bit rate off the eye diagram.
3. Identify the address bytes (constant across every packet).
4. Count payload bytes between address and CRC.
5. Verify: `crc16(address||payload, 0x1021, 0xFFFF)` MSB-first against the trailer.
6. Capture while reading the unit's display, to map payload bytes to fields.

All of 1–5 above are now answered from firmware (see rtl433-decoder.md) — this
manual URH pass is only needed if the firmware-derived values are ever in doubt.
