#!/usr/bin/env python3
"""Probe a Uponor Clean 1 IQ capture and test the on-air model two ways.

The firmware analysis (docs/nrf9e5-firmware.md) read the nRF905 config directly:
  channel 117 -> 868.2 MHz, 4-byte address EA EA EA EA, 32-byte payload,
  hardware CRC-16 (poly 0x1021, init 0xFFFF, over ADDRESS+PAYLOAD).
It also showed that NEITHER firmware applies a software Manchester step, which
puts the decoder's Manchester assumption in question ([U5] in the decoder).

This script settles it empirically. It FM-discriminates the capture, finds the
bursts, and for each one slices FSK bits and tries to locate the known
address EA EA EA EA:
  - NATIVE model  : the payload is raw nRF905 ShockBurst (NRZ). Look for the
                    address in the plain bitstream, then verify the hardware
                    CRC-16 over address+payload.
  - MANCHESTER    : Manchester-decode first (01->1, 10->0), then look for the
                    address and the "fd 7a" app header.
Whichever model actually finds EA EA EA EA (and a passing CRC-16) is the real
one. This closes [U5], and a passing CRC-16 closes [U4].

Usage:
    python3 tools/rtl433/probe_capture.py CAPTURE.cu8 [--rate HZ]
Sample rate is inferred from a "..._<n>k.cu8" suffix if --rate is omitted.
"""
import argparse
import re
import sys

try:
    import numpy as np
except ImportError:
    sys.exit("numpy required:  pip install numpy")

ADDR = bytes([0xEA, 0xEA, 0xEA, 0xEA])  # nRF905 RX/TX address from firmware


def crc16_ccitt(data, init=0xFFFF, poly=0x1021):
    c = init
    for b in data:
        c ^= (b << 8)
        for _ in range(8):
            c = ((c << 1) ^ poly) & 0xFFFF if (c & 0x8000) else (c << 1) & 0xFFFF
    return c


def revbits(b):
    return int('{:08b}'.format(b)[::-1], 2)


def load_iq(path):
    raw = np.fromfile(path, dtype=np.uint8).astype(np.float32) - 127.5
    return raw[0::2] + 1j * raw[1::2]


def find_bursts(mag, thresh, min_len):
    """Yield (start, end) sample spans where |signal| stays above thresh."""
    hot = mag > thresh
    idx = np.flatnonzero(np.diff(hot.astype(np.int8)))
    edges = idx + 1
    if hot[0]:
        edges = np.r_[0, edges]
    if hot[-1]:
        edges = np.r_[edges, len(hot)]
    for i in range(0, len(edges) - 1, 2):
        s, e = edges[i], edges[i + 1]
        if e - s >= min_len:
            yield s, e


def bits_to_bytes(bits, offset):
    out = bytearray()
    for i in range(offset, len(bits) - 7, 8):
        v = 0
        for k in range(8):
            v = (v << 1) | int(bits[i + k])
        out.append(v)
    return bytes(out)


def manchester_decode(bits):
    """01->1, 10->0; return (decoded_bits, illegal_fraction) over best phase."""
    best = None
    for phase in (0, 1):
        b = bits[phase:]
        n = (len(b) // 2) * 2
        pairs = b[:n].reshape(-1, 2)
        dec = []
        illegal = 0
        for hi, lo in pairs:
            if hi == 0 and lo == 1:
                dec.append(1)
            elif hi == 1 and lo == 0:
                dec.append(0)
            else:
                dec.append(0)
                illegal += 1
        frac = illegal / max(1, len(pairs))
        if best is None or frac < best[1]:
            best = (np.array(dec, dtype=np.int8), frac)
    return best


def search_addr(byte_streams, label, results):
    """Look for the 4-byte address in each byte stream (all bit offsets, both
    polarities already baked into byte_streams); on a hit, CRC-check."""
    for tag, data in byte_streams:
        for variant, needle in (("as-is", ADDR),
                                 ("bitrev", bytes(revbits(x) for x in ADDR))):
            pos = data.find(needle)
            if pos < 0:
                continue
            # address + 32 payload + 2 crc
            frame = data[pos:pos + 4 + 32 + 2]
            info = f"{label}/{tag}/{variant}: ADDR at byte {pos}"
            if len(frame) >= 38:
                body = frame[:4 + 32]
                trailer_be = (frame[36] << 8) | frame[37]
                calc = crc16_ccitt(body)
                if calc == trailer_be:
                    info += "  CRC-16 PASS"
                    info += "\n      payload(32B): " + frame[4:36].hex()
                    # in-payload CRC-16/CCITT trailer ([U4] #2): scan the last
                    # 2 payload bytes vs a CRC over a leading slice.
                    pl = frame[4:36]
                    for n in range(2, 31):
                        if crc16_ccitt(pl[:n]) == ((pl[30] << 8) | pl[31]):
                            info += f"\n      in-payload CRC-16/CCITT PASS over payload[:{n}]"
                            break
                else:
                    info += f"  crc calc={calc:04x} got={trailer_be:04x}"
            results.append(info)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("capture")
    ap.add_argument("--rate", type=float, default=0.0)
    ap.add_argument("--dev", type=float, default=38e3, help="FSK deviation Hz")
    args = ap.parse_args()

    rate = args.rate
    if not rate:
        m = re.search(r'_(\d+)k\.cu8$', args.capture)
        if not m:
            sys.exit("cannot infer sample rate from filename; pass --rate")
        rate = float(m.group(1)) * 1000.0

    iq = load_iq(args.capture)
    print(f"loaded {len(iq)} IQ samples @ {rate/1e6:.3f} MS/s")

    # FM discriminator -> instantaneous frequency (rad/sample).
    fm = np.angle(iq[1:] * np.conj(iq[:-1]))
    mag = np.abs(iq[1:])

    thresh = np.percentile(mag, 90) * 0.5
    bursts = list(find_bursts(mag, thresh, min_len=int(rate * 0.001)))
    print(f"found {len(bursts)} candidate bursts (>{thresh:.1f} mag, >1 ms)")

    results = []
    for bi, (s, e) in enumerate(bursts):
        seg = fm[s:e]
        seg = seg - np.mean(seg)  # remove residual carrier offset
        for kbps in (100, 50):
            spb = rate / (kbps * 1000.0)
            nbits = int(len(seg) / spb)
            if nbits < 80:
                continue
            centers = (np.arange(nbits) + 0.5) * spb
            samp = seg[np.clip(centers.astype(int), 0, len(seg) - 1)]
            for pol, bits in (("pol+", (samp > 0).astype(np.int8)),
                              ("pol-", (samp < 0).astype(np.int8))):
                tag = f"burst{bi}/{kbps}k/{pol}"
                # NATIVE: pack raw NRZ bits at every bit offset.
                native = [(f"{tag}/off{o}", bits_to_bytes(bits, o)) for o in range(8)]
                search_addr(native, "NATIVE", results)
                # MANCHESTER: decode then pack.
                dec, frac = manchester_decode(bits)
                manch = [(f"{tag}/mc/off{o}", bits_to_bytes(dec, o)) for o in range(8)]
                search_addr(manch, f"MANCH(illegal={frac:.2f})", results)

    print("\n=== ADDRESS / CRC HITS ===")
    if results:
        for r in results:
            print("  " + r)
    else:
        print("  no EA EA EA EA found in either model at any offset/polarity.")
        print("  (address may be sent LSB-first per byte, or the demod needs")
        print("   tuning - try adjusting --dev / the mag threshold.)")


if __name__ == "__main__":
    main()
