# Radio capture log — Uponor Clean 1 (868.35 MHz)

Chronological findings from RTL-SDR captures of the 868.35 MHz link between
inner and outer units. For the decoder spec and build instructions, see
[rtl433-decoder.md](rtl433-decoder.md).

---

## 0. Empirical results from the first real capture

A 21.6-minute continuous capture at 250 kS/s (`-f 868.35M`) was analysed offline
(FM discriminator + power-envelope scan in numpy, cross-checked with `rtl_433
-A`). Confirmed:

- **Cadence:** one radio event every **~62 s** (not the ~10 min that was
  assumed). ~20 events in the capture, very regular.
- **Each event is a two-packet exchange**, not a single frame:
  a **~6.2 ms** packet, a **~19 ms** gap, then a **~6.6 ms** packet. The two
  packets have very different RSSI (one much stronger) -> the near/far units
  (mainboard <-> tank sensor) answering each other. This matches an nRF905
  bidirectional ShockBurst link.
- **Modulation:** FSK, deviation **~+-38 kHz**. Per-packet length ~6.3 ms =>
  ~300 bits => **~35-40 bytes** at 50 kbps - consistent with the nRF905 default
  50 kbps and a ~32-byte payload + address + CRC.

Two gotchas this capture hit - **fix them on the next capture (section 1):**

1. **Carrier was ~76 kHz *below* the tune center**, landing right on the
   RTL-SDR's DC self-mixing spike. rtl_433's detector locked onto DC and saw
   nothing.
2. **Signal amplitude was low** (~11 counts above the 127.5 mid) - the RX gain
   was too conservative, so the burst sat near rtl_433's detection floor.

After frequency-shifting the signal off DC and lowering the thresholds, rtl_433
does detect and FSK-demodulate it:

    rtl_433 -r shifted.cu8 -s 250k -R 0 \
      -X 'n=uclean1,m=FSK_PCM,s=20,l=20,r=400' \
      -Y minmax -Y minlevel=-40 -Y minsnr=3 -M level

### Decoded from the second (off-center, high-gain) capture

A 5-minute capture at `-f 868.20M -s 1024k -g 49` (signal at +128 kHz, peak/noise
371x) demodulated cleanly. Per-packet analysis (FM discriminator + phase-searched
resample in numpy):

- **Line rate is 100 kbps, NOT 50 kbps.** Transition-spacing histogram clusters
  hard at 10 samples (@1.024 MS/s = 10.24 samp/bit = 100 kbps).
- **The payload is Manchester-encoded.** Every demodulated byte decomposes into
  `01`/`10` pairs; a Manchester decode gives only **~3–4 % illegal pairs**
  (random NRZ would be ~50 %). So the raw 100 kbps line carries **~50 kbps of
  Manchester data**, ~40 bytes/packet — which fits the nRF905 frame. The nRF9E5's
  8051 is doing the Manchester coding in software; the radio itself is NRZ.
- **Constant header/address:** after a `ababab` sync run every packet carries a
  constant `fd 7a … 83` region.
- **Payloads are near-static:** 3 of 4 consecutive events (~62 s apart) were
  **byte-identical** for ~38 bytes; only occasionally does the tail change. Makes
  sense — the tank state changes slowly. The varying tail bytes are the real
  telemetry.

**Impact on the decoder:** `tools/rtl433/uponor_clean1.c` assumes plain NRZ at
50 kbps. It must instead demod at **100 kbps** and **Manchester-decode** before
the address/CRC logic (rtl_433 flex `m=FSK_MC_ZEROBIT` / `DMC`, or a manual
Manchester pass in the C decoder). `[U1]` is resolved; `[U2]/[U3]` (address) ≈ the
constant `fd7a…83` bytes; `[U5]` payload ≈ ~30 bytes; `[U6]` field semantics still
need correlation against the unit's display.

Clean per-packet bit recovery is working; remaining work is exact byte alignment,
CRC location, and mapping the varying tail bytes to physical quantities.

---

## 0b. What the MC9S08 firmware says about the fields

The CPU flash (`dumps/u2-mc9s08gt-flash.s19`) was analysed headless in Ghidra
(`tools/ghidra/*.java`, run via `tools/analyze.sh`; see also
[docs/ghidra-firmware-analysis.md](ghidra-firmware-analysis.md)) to see whether the decoded
radio payload's byte layout could be recovered from the receiver side. The
short answer: **the on-air byte semantics are not in this flash** — but the
analysis is still useful because it pins down the architecture and gives the
decode *targets*.

Findings:

- **The SPI link is a custom MC9S08↔8051 message bus, not nRF905 register
  access.** SPI1C1 = 0xCC (MSTR=0) — the MC9S08 is the SPI *slave*; the nRF9E5's
  8051 is the master and owns the nRF905. So the MC9S08 never sees nRF905
  CONFIG/addresses, and the on-air framing we measured (`fd7a`, `bababa` sync,
  the `83` tag, and the software Manchester coding) is entirely the **8051's**
  doing and is **invisible in this dump**. This confirms the radio PHY has to be
  reverse-engineered from captures, not from the CPU flash.
- **SPI receive engine** (ISR at vector `0xFFE0` → `0xbd56`, armed by
  `FUN_bcdb`): buffer pointer at RAM `0x0600/0x0601`, byte counter `0x0602`, TX
  length `0x0603`, transfer-complete flag `0x0606`; a 16-bit running counter at
  `0x012e` tears the transfer down (disables SPI, `SPI1C1 &= 0x7f`) when it
  reaches `0x67a`. The ISR decompiles poorly (overlapping instructions) so
  byte-level tracing from it is unreliable.
- **Inter-MCU message format** (`FUN_db22`, message types `0x20 0x21 0x22 0x23
  0x26`; a 7-byte init message `02 00 2b 32 ff ff ff` at flash `0x80bb`): each
  message carries **four live 16-bit variables** from RAM `0x013e 0x0140 0x0109
  0x010b` plus a 2-byte header at `0x06be/0x06bf`. These are the CPU-side state
  words, i.e. what the CPU reports up to the 8051 — a strong hint at how many
  telemetry quantities exist, but not their on-air byte offsets.
- **Decode targets, initialised by `FUN_bdd8`:** a 12-entry × 8-byte alarm table
  at RAM `0x06f7` whose codes match the EEPROM alarm table exactly (`0x28`
  compressor, `0x29`–`0x2d` MV1–5, `0x2f` EEPROM error, `0x33`, …), plus a large
  setpoint/timer block at `0x055c`–`0x05ea` (durations such as 600, 900, 3000,
  6000, `0x1518`=5400). The decoded payload codes should index into this alarm
  table and the phase names.

**Conclusion for [U6]:** the payload→field binding lives on the 8051, not here,
so the last step is a **display-correlated capture** — change the water level /
force an alarm on the unit and watch which decoded payload byte moves. The
firmware gives us what those bytes *mean* once we find them (the alarm-code and
setpoint tables above), not their positions.

---

## 0c. Decoded reference frames (baseline)

These are the two frames actually recovered on-air, to serve as a **fixed diff
target** for future display-correlated captures. Obtained with the flex decoder
(demodulator-level Manchester, see section 2 of rtl433-decoder.md):

    rtl_433 -f 868.20M -Y minmax \
      -X 'n=uclean1,m=FSK_MC_ZEROBIT,s=10,r=100,bits>=200,invert'

Every ~62 s exchange is one **poll** + one **response**. Across ~35 min of
capture (two sessions, ~30 exchanges) both frames were **byte-identical** — the
only differences were isolated single-bit demod errors in individual noisy
packets (e.g. one poll read `…49 2e 61 b1 ef…` instead of `…49 2e 71 b0 2f…`).
Align each row on the `fd 7a` header first (the frame starts a few bits into a
byte, so byte 0 of the row is meaningless — see section 2 of rtl433-decoder.md).

Header (constant on every frame): `fd 7a ba ba ba 83`

| frame    | payload bytes (after the 6-byte header) |
| ---      | --- |
| poll     | `73 1b 93 e0 03 5b b0 08 d2 c0 49 2e 71 b0 2f 4f fc bd 2e 2a` … |
| response | `33 1b a3 f0 08 d2 e0 03 5b 80 1c 11 71 f5 1d 2c ff ef 7f 3b` … |

(The frames continue for ~13 more stable bytes into a low-SNR tail; the exact
end-of-payload / CRC boundary is still `[U5]`/`[U4]`, so only the confident head
is tabulated.)

### What the structure tells us (no display correlation yet)

- **No counter, nonce, or rolling code.** Nothing increments between exchanges;
  this is pure repeated telemetry. (Good for Home Assistant — no replay/rolling
  handling — but it means fields can only be found by *changing* the unit.)
- **Byte 1 = `0x1b`** in both frames — a constant, likely a protocol/type byte.
- **A pair of 3-byte fields is swapped between poll and response** — the classic
  source/destination signature, i.e. the two units' **node IDs**:
  - poll carries `(03 5b b0)(08 d2 c0)`, response carries `(08 d2 e0)(03 5b 80)`.
  - So `03 5b _` = one unit, `08 d2 _` = the other; the request/reply swaps them.
- Bytes 0/2/3 differ between the two (`73 93 e0` vs `33 a3 f0`) — a
  direction/type/flags region.
- Everything from byte ~10 onward is the telemetry payload proper, still static
  and therefore still unmapped (`[U6]`).

### How to use this baseline

Run a **display-correlated capture** (section 1 of rtl433-decoder.md), then diff the recovered frame
against the row above. Any byte that moves is that field; the firmware tables in
section 0b then say what it means (alarm byte → alarm table; a setpoint edit should shift
by the known step). Change **one** thing at a time.

---

## 0d. Advances: frame structure & checksum

Two things unblocked a deeper look at the payload: a **standalone raw-IQ
demodulator** (so we no longer depend on rtl_433's flex decoder to get bits), and
a **1.5 h idle log** of 730 frames that exposed which bytes are constant vs.
variable. Results below.

### A working demod recipe (numpy, no rtl_433)

Given one clean `.cu8` capture of a single poll+response exchange, this reproduces
the exact rtl_433 frames with **zero illegal Manchester pairs**:

1. Load the interleaved unsigned-8-bit IQ, recenter on 127.5, make it complex.
2. FM-discriminate: `d = np.angle(iq[1:] * np.conj(iq[:-1]))`.
3. Find the bursts (contiguous high-magnitude spans).
4. Recover the chip clock by **fixed-rate resampling** at ~**10.0 samples/chip**
   (1 MS/s ÷ ~100 kbps), scanning the start phase in ~0.5-sample steps and the
   unit in 0.05-sample steps; hard-slice each chip on the sign of `d`.
5. Manchester-decode the chips (`01 → 1`, `10 → 0`), scan both bit-parities, and
   search for the header bits `fd 7a ba ba ba 83`.

The winning parameters for `g001_868.2M_1000k.cu8` were `unit≈10.0`, and both
bursts decoded with **0** illegal pairs, byte-for-byte matching the section-0c
frames. This confirms the flex-decoder output is faithful and gives a tool that
can attack **bit alignment** directly (see the checksum note below).

### The poll payload is plaintext — a doubled 13-byte record

The idle poll is not whitened or scrambled: it is literally the **same 13-byte
record sent twice**, with flags and a trailer appended (33 payload bytes total
after the header):

    [73 1b 83 ff ff ff f0 03 e4 00 49 6d a5]  record (13 B, idle template)
     83                                         separator
    [73 1b 83 ff ff ff f0 03 e4 00 49 6d a5]  record repeat
     bc                                         flag byte (bc ↔ ac, one bit)
     95 1f                                      constant marker on every poll
     f6                                         flag byte (f6 ↔ e6, one bit)
     0c a2                                      2-byte trailer (content-dependent)

Record layout (13 B): `73 1b`(magic) · `TT`(status: `83` idle / `93` active) ·
4-byte field · 3-byte field · `49`(const) · 2-byte tail. During a treatment cycle
**record A updates first while record B still holds the idle template** — so the
frame carries **two records** (current + counterpart), consistent with the
source/destination node-ID reading in section 0c.

### The response frame is 100 % static

Across the whole 1.5 h log, all **136** response frames were byte-identical; every
bit of variation lived in the poll stream (30 distinct poll frames out of 594).
So in normal idle only the poll side is worth diffing.

### The 2-byte trailer is NOT a standard CRC `[U4]`

Using a **differential** method — comparing frame-pair deltas, which cancels the
constant header/marker bytes and is independent of the CRC `init`/`xorout` — the
trailer (bytes 31–32) was tested against:

- **CRC-16**: all **65536** polynomials × both byte-endiannesses × all
  reflection settings (refin/refout) × forward **and** reversed message-byte
  order, at both whole-frame and per-record scope → **0 matches**.
- **CRC-8** on each trailer byte independently → 0 matches.
- **sum8, sum16, xor8, Fletcher-16, Adler** → 0 matches.

Yet two idle frames that differ by only **one bit** (byte 27 `bc↔ac`, or byte 30
`f6↔e6`) still produce different trailers, so it **is** a content-dependent check
— just a **nonlinear / proprietary** one, not a bit-serial CRC. The de-whitening /
bit-alignment question is therefore closed: the frame is already plaintext, so the
remaining blocker is the check *function*, which likely needs the nRF9E5's 8051
image (Path C) or many labelled captures to pin down.

**Practical upshot:** we do **not** need the trailer to read telemetry. The
payload is plaintext, so fields can be mapped purely by correlating byte changes
with device state (the `testknapp` / valve-cycle experiment in section 0b / rtl433-decoder.md section 1).

---

## 0e. First display-correlated event: a full batch, counter 1257 → 1258

A **~20 h continuous decode log** (`uclean1-20260702-2128.json`, 2597 frames,
2026-07-02 21:29 → 2026-07-03 17:35, board B) was taken across a real change of
the display's *satsräknare* (batch counter): it read **1257** at the start and
**1258** at the end. This is the first capture that brackets a confirmed counter
tick, so it directly answers whether the counter is on the radio.

**The counter value is NOT in the radio payload.** The two heartbeat frames are
**byte-identical across the entire span**, including before and after the tick:

| frame    | occurrences | payload (33 B) |
| ---      | ---         | --- |
| poll     | 1029 identical | `731b93e0035bb008d2c0492e71b02f4ffcbd2e2aff97aff65ffffde9bfdafbbb42` |
| response |  990 identical | `331ba3f008d2e0035b801c1171f51d2cffef7f3bbcf7de9fadb5fbabcfa2ffccd3` |

If the satsräknare were encoded in the heartbeat, these bytes would differ
before vs. after 1257 → 1258. They do not — so the number is not broadcast. That
matches section 0b: the counter lives in the MC9S08 (U2) and is only reachable
over the serial `CODE` interface, not the nRF905 link.

**What the tick *does* look like on-air:** every byte of variation in the whole
log is confined to a single **~55-minute burst, 23:03 → 23:58 on 2026-07-02** —
the "active record" poll family from section 0d (status byte `83`→`93`, changing
tails):

    731b83fffffff003e400496da583 731b83fffffff003e400496da5 <tail>   (283× + 67× + 51× + 49× + …)
    731b93e0035bb003e400491537…  (active variants, 25× + 10× + …)

That burst is the **treatment batch running**; when it completed, the display
went 1257 → 1258. Before and after it, the radio drops back to the two static
heartbeats. So this is the section-0d prediction observed end-to-end: during a
cycle **record A goes active while record B holds the idle template**, and the
cycle's completion is what advances the counter.

**Consequence for Home Assistant:** the radio reveals batch *events* (a burst =
a cycle in progress → a `binary_sensor` for "treating"), but **not the counter
value**. To surface the numeric satsräknare you still need the serial `CODE`
path (Path A / [J5](u2-serial-protocol.md)). The `[U6]` field-mapping goal is
unchanged; this capture just rules the counter out of the heartbeat and gives a
timestamped batch window (23:03–23:58) to diff the active records against idle.
