# rtl_433 decoder for the Uponor Clean 1 (868.35 MHz)

Goal: capture the 868.35 MHz link between the Clean 1 inner and outer units with
an RTL-SDR, decode it to JSON with [rtl_433](https://github.com/merbanan/rtl_433/),
and forward to Home Assistant over MQTT.

The transmitter is a Nordic **nRF9E5** (= nRF905 transceiver + 8051 MCU), so the
air protocol is nRF905 "ShockBurst": **GFSK**, NRZ line coding (NOT OOK, NOT
Manchester), default **50 kbps**, carrier **868.35 MHz**.

nRF905 ShockBurst frame:

    PREAMBLE(10 bits) | ADDRESS(1-4 B, default 4) | PAYLOAD(1-32 B) | CRC(0/8/16, default 16)

CRC-16: poly `0x1021` (x^16+x^12+x^5+1), init `0xFFFF`, MSB-first, over
ADDRESS+PAYLOAD.

The decoder skeleton lives at `tools/rtl433/uponor_clean1.c`.

---

## 1. Capture raw IQ for analysis

You need real captures before the decoder can be validated. Based on the empirical
results in [radio-capture-log.md](radio-capture-log.md), capture so the signal is
**off the DC spike** and **not gain-starved**:

    rtl_sdr -f 868.20M -s 1024000 -g 49 -n $((1024000*300)) uclean_offset.cu8

- `-f 868.20M`  deliberately tune ~150 kHz low. The real carrier (~868.27 MHz,
  measured) then lands ~+70 kHz from center - clear of the 0 Hz DC spike.
- `-s 1024k`    1.024 Msps - wide enough for +-38 kHz FSK with room to keep the
  signal away from DC.
- `-g 49`       high gain; the first capture was ~15 dB under-driven. Back off if
  you see clipping (many samples pinned at 0 or 255).
- `-n ...`      ~300 s => ~5 of the ~62 s exchanges. Increase for more.

The R820T tuner has no hardware offset-tune, so use the frequency offset above
rather than `-t offset_tune=1`.

You can also let rtl_433 save each detected event, but only with the thresholds
from the capture log:

    rtl_433 -f 868.20M -s 1024k -g 49 -S all \
      -Y minmax -Y minlevel=-40 -Y minsnr=3

Each `g###_868.20M_1024k.cu8` file is one event (containing the 2-packet
exchange).

---

## 2. Quick first pass with the flex decoder

Before integrating C, sanity-check demodulation with rtl_433's built-in flex
decoder (`-X`). The best option is to let the flex demodulator do the Manchester
decode for you, using the `FSK_MC_ZEROBIT` modulation:

    rtl_433 -f 868.20M -Y minmax \
      -X 'n=uclean1,m=FSK_MC_ZEROBIT,s=10,r=100,bits>=200,invert'

- `m=FSK_MC_ZEROBIT`  a **demodulator-level** Manchester slicer - it decodes
  Manchester straight from the pulse timing, so the output is the **decoded
  bytes**, not the raw on-air bits. This is *not* the `decode_mc` post-filter
  (see below), which does not work here.
- `s=10`       the half-bit period is 10 us (one 100 kbps raw chip). The slicer
  pairs chips into data bits, so rows come out ~326 bits (half of the ~650 raw).
- `r=100`      reset limit; splits the two packets of an exchange (~19 ms apart).
- `invert`     the slicer's default polarity is the complement of ours; `invert`
  makes it match the `01`->1 / `10`->0 convention, so the decoded header prints
  **verbatim** as `fd 7a ba ba ba 83 ...`.
- `-Y minmax`  **selects the FSK peak detector.** Without it the default detector
  fragments the weaker (far-unit) packet. With it, a 4-event replay of
  `uclean_offset.cu8` yields all 8 packets (4 events x near+far) - **full recall**.
- `bits>=200`  drops the noise rows. Real decoded packets are ~320-330 bits.

`FSK_MC_ZEROBIT` locks its phase on a leading zero bit, so an occasional row is
off by one bit - just search for `fd 7a` (as the C decoder does) rather than
assuming byte 0 is the header. Replay a saved capture instead of live SDR:

    rtl_433 -r uclean_offset.cu8 -s 1024k -Y minmax \
      -X 'n=uclean1,m=FSK_MC_ZEROBIT,s=10,r=100,bits>=200,invert'

### Seeing the raw on-air bits instead

If you want the raw (still Manchester-*encoded*) stream to inspect by eye
(`01`->1, `10`->0), use `FSK_PCM` at the full 100 kbps line rate:

    rtl_433 -f 868.20M -Y minmax -X 'n=uclean1,m=FSK_PCM,s=10,l=10,r=100,bits>=400'

Here rows are ~635-654 raw bits and `bits>=400` filters the noise. You can also
add `,preamble={32}55599566` to strip the leading `0xBA` run and align rows to the
header - that value is `fd 7a` in the raw encoded domain (`fd 7a` = `11111101
01111010`, encode `1`->`01`/`0`->`10` => `0x5559 0x9566`). But keep the preamble
**optional**: it is an exact match over 32 raw bits, so a single noise-flipped bit
in the sync drops the whole packet and you can lose the weaker packet of an
exchange. `FSK_MC_ZEROBIT` above is the better choice - it decodes natively and
keeps full recall.

### Why not `decode_mc`?

The flex `decode_mc` (a post-filter on already-sliced NRZ bits, *not* a
modulation) does **not** work here: it aborts at the first Manchester violation
and only tries phase 0, so these phase-offset, slightly-noisy packets collapse to
empty rows. `FSK_MC_ZEROBIT` avoids that by working at the demodulator/timing
level. For the fully validated payload (with the violation-tolerant two-phase
pass and CRC gating) use the C decoder (`-R <num>`).

---

## 3. Build and test the C decoder

The C decoder must be compiled inside an rtl_433 source checkout.

1. Clone rtl_433 and copy the decoder in:

       git clone https://github.com/merbanan/rtl_433
       cp tools/rtl433/uponor_clean1.c rtl_433/src/devices/

2. Register it. rtl_433 keeps a central list of decoders:
   - add `DECL(uponor_clean1)` to `include/rtl_433_devices.h`
     (in the same block as the other `DECL(...)` entries), and
   - add `src/devices/uponor_clean1.c` to the device source list in
     `src/CMakeLists.txt` (and the autotools `Makefile.am` if building that way).

3. Build:

       cd rtl_433 && mkdir -p build && cd build
       cmake .. && make -j

4. Verify the decoder registered and run it against a capture:

       ./src/rtl_433 -R 0       # lists all decoders; find the Uponor Clean1 number
       ./src/rtl_433 -r ../../capture.cu8 -R <num> -F json

    The decoder ships with `.disabled = 1` (skeleton), so enable it explicitly
    with `-R <num>` until it is validated and the protocol number is known.

5. Once validated, output to MQTT for Home Assistant:

       rtl_433 -f 868.35M -R <num> -F "mqtt://localhost:1883,events=uclean1/events"

    Then add an MQTT sensor in Home Assistant pointing at that topic.

---

## 4. State of the decoder / what is still needed

`tools/rtl433/uponor_clean1.c` is a **skeleton**. It compiles and demonstrates
the full pipeline (preamble search -> address match -> CRC-16 check -> JSON),
but it has **not** been validated against real RF and ships disabled.

To finish it, resolve the UNKNOWNS (listed in the file header) from captures or
firmware analysis and edit the corresponding `#define`s / constants:

| Unknown | Symbol in uponor_clean1.c                | Source |
|---------|------------------------------------------|--------|
| [U1] data rate     | `short_width`/`long_width` (20 vs 10) | capture eye diagram / CONFIG |
| [U2] address width | `UCLEAN1_ADDR_BYTES`                  | CONFIG RX_AFW / capture |
| [U3] address bytes | `uclean1_address[]`                   | TX_ADDRESS / capture |
| [U4] CRC width     | `UCLEAN1_CRC_BITS`                    | CONFIG CRC_EN/CRC_MODE / capture |
| [U5] payload length| `UCLEAN1_PAYLOAD_BYTES`               | CONFIG RX_PW / capture |
| [U6] field meaning | `data_make(...)` + `output_fields[]`  | capture vs unit display |
| [U7] whitening     | (none assumed)                        | confirm via CRC match |
| [U8] bit order     | (MSB-first assumed)                   | confirm via CRC match |

Do not invent these values - a wrong address or CRC config silently drops every
packet, and wrong field scaling produces plausible-but-false readings.

For chronological capture findings, frame dumps, and analysis notes, see
[radio-capture-log.md](radio-capture-log.md).
