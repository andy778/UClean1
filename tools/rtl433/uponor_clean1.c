/** @file
    Uponor Clean 1 wastewater treatment unit - 868.35 MHz telemetry link.

    Copyright (C) 2026 Andy B

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Uponor Clean 1 - inner/outer unit radio link.

The Clean 1 links the outer (tank) unit and the inner (indoor display) unit.
U1 on the PCB is a Nordic nRF9E5 (nRF905 transceiver + 8051 MCU). The radio PHY
is nRF905: GFSK, 868 MHz band. Everything below was measured from real RTL-SDR
captures (see docs/rtl433.md, "Empirical results"), NOT assumed from the
datasheet - and several datasheet-default assumptions turned out to be wrong.

CONFIRMED from captures
-----------------------
- Carrier   : ~868.3 MHz (a specific nRF905 channel).
- Modulation: FSK, deviation ~+-38 kHz.
- LINE RATE : 100 kbps (10 us/bit), NOT the nRF905 50 kbps default.
- CODING    : the payload is MANCHESTER-encoded (the nRF9E5 firmware does this
              in software - the radio itself is NRZ). Raw 100 kbps line =>
              ~50 kbps of Manchester data, ~40 bytes/packet. This is the single
              most important correction over the old skeleton, which assumed
              plain NRZ.
              Convention observed: raw "01" -> 1, raw "10" -> 0.
- FRAMING   : each transmit event (every ~62 s) is a TWO-packet exchange -
              packet A (~6.2 ms) then a ~19 ms gap then packet B (~6.6 ms), with
              very different RSSI = the near/far units answering each other.
- SYNC/HDR  : after a repeating 0xBA run, every Manchester-decoded packet begins
              with a constant header:  fd 7a  ba ba ba  83  <payload...>.
              The 0xfd 0x7a pair is used here as the frame anchor.
- PAYLOAD   : ~30 bytes after the header. Across consecutive events the payload
              is near-static (3 of 4 events were byte-identical), i.e. it is
              slowly-changing telemetry, not a counter/nonce.

STILL UNKNOWN (do not invent)
-----------------------------
  [U5] exact payload length / where the payload ends and any CRC begins.
  [U4] CRC: nRF905 hardware CRC covers the *on-air* (pre-Manchester) bytes, so it
       is not visible in the Manchester-decoded stream; there may additionally be
       an application checksum inside the payload. Unconfirmed - not gated on.
  [U6] field semantics: which decoded byte is water level / temperature / phase /
       alarm code / pump state. The MC9S08 firmware was checked (see docs/rtl433.md
       "0b. What the MC9S08 firmware says") and the payload byte layout is NOT in
       it: the CPU is only an SPI *slave* on a custom MC9S08<->8051 message bus, so
       the on-air framing and byte packing are done by the nRF9E5's 8051, not the
       CPU. The firmware does give the decode *targets* - the alarm code table
       (0x02 no-radio, 0x28 compressor, 0x29-0x2D MV1-5, 0x2F EEPROM error, ...)
       and the phase names, which the payload codes should index into. So [U6] must
       be resolved by a display-correlated capture: change the level / force an
       alarm and watch which decoded payload byte moves.

Because [U5]/[U6] are open, this decoder emits the constant header plus the raw
Manchester-decoded payload as a hex string, so captures can be correlated with
the display. It ships disabled until the fields and CRC are pinned down.

Flex-decoder equivalent for a first capture pass. Tune ~150 kHz low so the
carrier lands off the RTL-SDR DC spike; -Y minmax selects the FSK peak detector
(otherwise the weak far-unit packet fragments). Best option - let the flex
demodulator do the Manchester decode, so the payload comes out readable:
  rtl_433 -f 868.20M -Y minmax \
    -X 'n=uclean1,m=FSK_MC_ZEROBIT,s=10,r=100,bits>=200,invert'
FSK_MC_ZEROBIT is a demod-LEVEL Manchester slicer (decodes from pulse timing),
so s=10 is the half-bit period and the output is the DECODED bytes (~326 bits,
half of ~650). "invert" makes it match our raw 01->1 / 10->0 convention, so the
fd 7a ba ba ba 83 header prints verbatim. Full recall (all 8 packets of a 4-event
replay). Its phase lock assumes a leading zero bit, so a row may be off by one
bit - just search for fd7a as this decoder does. NOTE this is NOT the same as the
"decode_mc" post-filter, which aborts at the first Manchester violation and only
tries phase 0, so on these phase-offset, slightly-noisy packets it collapses to
empty rows - that filter is unusable here, which is why this C decoder rolls its
own tolerant, two-phase pass.
To instead see the RAW on-air bits (Manchester still encoded, decode by eye
01->1/10->0), use FSK_PCM at the full 100 kbps line rate:
  rtl_433 -f 868.20M -Y minmax -X 'n=uclean1,m=FSK_PCM,s=10,l=10,r=100,bits>=400'
*/

#include "decoder.h"

// Raw line rate 100 kbps => 10 us/bit at the PCM demod.
#define UCLEAN1_BIT_US 10

// Manchester-decoded header anchor (see notes above): fd 7a precedes the packet.
static uint8_t const uclean1_sync[] = {0xfd, 0x7a};

// Conservative bounds on the Manchester-decoded packet (bytes).
#define UCLEAN1_MIN_BYTES 20
#define UCLEAN1_MAX_BYTES 64

// Read bit `i` (MSB-first) from a packed byte buffer.
static inline int uclean1_bit(uint8_t const *buf, unsigned i)
{
    return (buf[i >> 3] >> (7 - (i & 7))) & 1;
}

// Manchester-decode `nbits` raw bits from `raw` (packed) starting at raw-bit
// `start`, convention raw 01->1, raw 10->0. Writes packed decoded bits to `out`.
// Returns number of decoded bits; *illegal counts 00/11 violations.
//
// rtl_433 does ship bitbuffer_manchester_decode(), which uses this same
// convention (it emits the second bit of each pair). We deliberately don't use
// it here: it aborts at the FIRST Manchester violation and decodes a single
// phase alignment. Real captures of this link carry ~3-4% illegal pairs (noise)
// at an unknown bit phase, so we instead tolerate violations, count them as a
// quality metric, and let the caller try both phases and keep the cleaner one.
static unsigned uclean1_manchester(uint8_t const *raw, unsigned start,
        unsigned nbits, uint8_t *out, unsigned out_cap_bits, unsigned *illegal)
{
    unsigned n = 0;
    *illegal = 0;
    for (unsigned i = start; i + 1 < nbits && n < out_cap_bits; i += 2) {
        int a = uclean1_bit(raw, i);
        int b = uclean1_bit(raw, i + 1);
        int d;
        if (a == 0 && b == 1)
            d = 1;              // 01 -> 1
        else if (a == 1 && b == 0)
            d = 0;              // 10 -> 0
        else {                  // 00 / 11 : Manchester violation
            (*illegal)++;
            d = 0;
        }
        if (d)
            out[n >> 3] |= (uint8_t)(1 << (7 - (n & 7)));
        else
            out[n >> 3] &= (uint8_t)~(1 << (7 - (n & 7)));
        n++;
    }
    return n;
}

// Bit-level search for `pat` (pat_bits long) in packed `buf` of `nbits`.
// Returns the bit index of the match, or nbits if not found.
static unsigned uclean1_bitsearch(uint8_t const *buf, unsigned nbits,
        uint8_t const *pat, unsigned pat_bits)
{
    if (pat_bits > nbits)
        return nbits;
    for (unsigned off = 0; off + pat_bits <= nbits; ++off) {
        unsigned k = 0;
        for (; k < pat_bits; ++k) {
            if (uclean1_bit(buf, off + k) != uclean1_bit(pat, k))
                break;
        }
        if (k == pat_bits)
            return off;
    }
    return nbits;
}

static int uponor_clean1_decode(r_device *decoder, bitbuffer_t *bitbuffer)
{
    int ret = DECODE_ABORT_EARLY;

    for (unsigned row = 0; row < bitbuffer->num_rows; ++row) {
        unsigned raw_bits = bitbuffer->bits_per_row[row];

        // A ~6.5 ms Manchester packet at 100 kbps is ~650 raw bits.
        if (raw_bits < UCLEAN1_MIN_BYTES * 16) {
            ret = DECODE_ABORT_LENGTH;
            continue;
        }

        // Copy the raw row out as packed bytes.
        uint8_t raw[128] = {0};
        unsigned copy_bits = raw_bits > sizeof(raw) * 8 ? sizeof(raw) * 8 : raw_bits;
        bitbuffer_extract_bytes(bitbuffer, row, 0, raw, copy_bits);

        // Manchester-decode both bit-phase alignments; keep the cleaner one.
        uint8_t dec[UCLEAN1_MAX_BYTES + 4] = {0};
        uint8_t best[sizeof(dec)] = {0};
        unsigned best_bits = 0, best_ill = ~0u;
        for (unsigned phase = 0; phase < 2; ++phase) {
            unsigned ill = 0;
            unsigned nb = uclean1_manchester(raw, phase, copy_bits, dec,
                    sizeof(dec) * 8, &ill);
            if (ill < best_ill) {
                best_ill = ill;
                best_bits = nb;
                memcpy(best, dec, sizeof(dec));
            }
        }

        // A genuine Manchester packet has very few violations. Random noise
        // demodulated at 100 kbps sits near 50%. Require < ~15%.
        if (best_bits < UCLEAN1_MIN_BYTES * 8
                || best_ill * 100 > (best_bits / 2) * 15) {
            ret = DECODE_ABORT_EARLY;
            continue;
        }

        // Find the fd 7a frame anchor in the decoded stream.
        unsigned pos = uclean1_bitsearch(best, best_bits,
                uclean1_sync, sizeof(uclean1_sync) * 8);
        if (pos >= best_bits) {
            decoder_log(decoder, 2, __func__, "sync fd7a not found");
            ret = DECODE_ABORT_EARLY;
            continue;
        }

        // Header = fd 7a ba ba ba 83  (6 bytes = 48 bits); payload follows.
        unsigned hdr_bits = 6 * 8;
        unsigned payload_start = pos + hdr_bits;
        if (payload_start + UCLEAN1_MIN_BYTES * 8 > best_bits) {
            ret = DECODE_ABORT_LENGTH;
            continue;
        }

        // Byte-align header + payload for output. Extract up to what we have.
        unsigned avail_bits = best_bits - pos;
        unsigned avail_bytes = avail_bits / 8;
        if (avail_bytes > UCLEAN1_MAX_BYTES)
            avail_bytes = UCLEAN1_MAX_BYTES;
        uint8_t frame[UCLEAN1_MAX_BYTES] = {0};
        for (unsigned i = 0; i < avail_bytes; ++i) {
            uint8_t v = 0;
            for (unsigned k = 0; k < 8; ++k)
                v = (uint8_t)((v << 1) | uclean1_bit(best, pos + i * 8 + k));
            frame[i] = v;
        }

        // frame[0..5] = header, frame[6..] = payload.
        unsigned payload_len = avail_bytes > 6 ? avail_bytes - 6 : 0;

        char header_str[6 * 2 + 1];
        for (unsigned i = 0; i < 6; ++i)
            snprintf(header_str + i * 2, 3, "%02x", frame[i]);

        char payload_str[UCLEAN1_MAX_BYTES * 2 + 1];
        for (unsigned i = 0; i < payload_len; ++i)
            snprintf(payload_str + i * 2, 3, "%02x", frame[6 + i]);

        /* clang-format off */
        data_t *data = data_make(
                "model",        "",             DATA_STRING, "Uponor-Clean1",
                "header",       "Header",       DATA_STRING, header_str,
                "payload",      "Payload",      DATA_STRING, payload_str,
                "payload_len",  "Payload bytes",DATA_INT,    (int)payload_len,
                "mc_illegal",   "MC violations",DATA_INT,    (int)best_ill,
                NULL);
        /* clang-format on */
        decoder_output_data(decoder, data);
        return 1;
    }

    return ret;
}

static char const *const uponor_clean1_output_fields[] = {
        "model",
        "header",
        "payload",
        "payload_len",
        "mc_illegal",
        // Real sensor fields go here once [U6] is resolved, e.g.:
        // "level_pct", "temperature_C", "phase", "alarm", "pump_on",
        NULL,
};

// 100 kbps line rate => 10 us/bit. FSK_PULSE_PCM with short==long==bit period.
// reset_limit ends the packet after a run with no transition; Manchester
// guarantees a transition at least every 2 bits, so ~100 us (10 bits) is safe
// and still splits the two packets of an exchange (~19 ms apart) into rows.
r_device const uponor_clean1 = {
        .name        = "Uponor Clean 1 wastewater unit (nRF905, 868 MHz, 100kbps Manchester) [UNVERIFIED]",
        .modulation  = FSK_PULSE_PCM,
        .short_width = UCLEAN1_BIT_US,
        .long_width  = UCLEAN1_BIT_US,
        .reset_limit = 100,
        .decode_fn   = &uponor_clean1_decode,
        .disabled    = 1,   // keep disabled until CRC + fields are validated
        .fields      = uponor_clean1_output_fields,
};
