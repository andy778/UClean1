---
name: rtl433-uponor-clean1
description: Use when implementing, extending, reviewing, or debugging an rtl_433 decoder (or any TX/RX implementation) for the Uponor Clean 1's 868.2 MHz radio link — covers where the protocol facts live, the reference decoder, capture/test-vector workflow, and the review process this project uses. Trigger on requests to add/fix a field in the decoder, build a TX proof-of-concept, capture a new alarm type, or port the protocol to another language/platform.
---

# Implementing rtl_433 support for the Uponor Clean 1

This project reverse-engineered the radio link from firmware, not by guessing
at captures — every protocol fact is traceable to a disassembled CPU function
or a specific live capture. Preserve that: don't add a field or behavior you
can't cite, and don't remove a caveat because it's inconvenient.

## Start here, always

Read **[`docs/rtl433-implementation-spec.md`](../../docs/rtl433-implementation-spec.md)**
first. It's self-contained — physical layer, frame format, byte-exact payload
map, test vectors, and the one recipe for the still-uncaptured `device_fault`
alarm type. Don't re-derive the protocol from the narrative research docs
(`docs/ghidra/`, `docs/radio-capture-log.md`, etc.) — those are provenance for
the spec, not a second source of truth. If you find the spec and a research
doc disagree, that's a bug in one of them — flag it, don't silently pick one.

## Where the code lives

The decoder is **not in this repo**. It's in a fork:
[andy778/rtl_433, branch `add-uponor-clean1`](https://github.com/andy778/rtl_433/blob/add-uponor-clean1/src/devices/uponor_clean1.c),
a fork of upstream [merbanan/rtl_433](https://github.com/merbanan/rtl_433) —
not yet upstreamed (spec §9 covers why: one alarm type still unconfirmed on
air). Clone/work in that repo for decoder changes; this repo (`UClean1`) is
where the protocol research and spec live.

## Workflow for a decoder change

1. **Branch off `add-uponor-clean1`**, not `master` — that branch is the
   accumulated, CONTRIBUTING.md-compliant decoder state.
2. **Cross-check against the spec** before writing code — most bugs in this
   decoder's history came from not doing this (e.g. an early version reported
   the constant frame address as `id` instead of the per-device `node_src`;
   see spec §5).
3. **Build and verify against real captures**, not just a clean compile:
   ```bash
   cd build && make -j4 rtl_433
   ./src/rtl_433 -r <capture>.cu8 -Y minmax -F json | grep -i uponor
   ```
   Use the test vectors in spec §6 if you don't have a live capture — replay
   them through the CRC/parsing logic directly, or construct a synthetic
   `.cu8` if you need to exercise the demodulator too.
4. **`clang-format -i`** before committing — this decoder targets upstream
   merbanan/rtl_433 conventions (see that repo's `docs/CONTRIBUTING.md`),
   not this project's own style.
5. **Get an independent review pass** before opening the PR. This project's
   practice: dispatch a second review with a *different* model than the one
   that wrote the change — one pass for correctness (bounds, buffer safety,
   state leaks across the decode loop), one for upstream convention
   compliance (field naming, `output_fields[]` matching `data_make()`, return
   value idiom). Both passes should read the code directly and cite line
   numbers, not speculate.
6. **Open the PR against `add-uponor-clean1`** (not upstream — see "Where the
   code lives"), with a test plan showing what was actually run, not just
   "should work."
7. If the change affects protocol understanding (new field, corrected
   semantics, a newly confirmed value), **update the spec in this repo in the
   same work session** — a decoder fix that isn't reflected in the spec will
   just get re-broken by the next person who trusts the spec.

## Common pitfalls (all previously hit in this project)

- **`id` must be `node_src`, never the frame address.** The address
  (`EA EA EA EA`) is a firmware-hardcoded constant, identical on every unit
  ever shipped — using it as `id` collides all devices into one.
- **Stale tail bytes (`payload[11+N..31]`) are not data.** They vary
  run-to-run for identical logical messages. Never decode them, never use
  them as a freshness/nonce signal.
- **Validate `payload[0] == 12 + N`, not just the CRC.** CRC passing only
  proves the bytes weren't corrupted in transit — it says nothing about
  whether your address-sync landed on the right offset.
- **Don't implement software Manchester encoding for TX.** The on-air
  Manchester coding is the nRF905 hardware's own behavior; the firmware
  writes plain bytes. A TX implementation using a real nRF905 chip and the
  register bytes in spec §2 should reproduce the correct on-air waveform
  automatically.
- **A `bitbuffer_invert()` call must be undone on every exit path**, not just
  the success path, if you're working in the rtl_433 decode loop — an
  inverted buffer leaks into the next row's parse otherwise. Trace every
  `continue`/`return` in the loop when touching this logic.

## If you're building something other than the rtl_433 decoder

The spec (§1–§6) is decoder-agnostic — same frame format and test vectors
apply whether you're writing a Python/GNU Radio flowgraph, an ESP32 TX using
a real nRF905 module, or a from-scratch RX. Spec §9 flags what's genuinely
unverified (no third-party TX has ever been tried against the real panel) —
if you get a TX proof-of-concept working, that result is worth documenting
back into this repo's `docs/`, since it upgrades an assumption to a
confirmed fact for everyone after you.

## Definition of done for a decoder change

- [ ] Builds clean, `clang-format` applied
- [ ] Verified against a real capture or the spec's test vectors, not just
      "compiles"
- [ ] Reviewed by a second pass (correctness + convention), findings resolved
- [ ] PR opened against `add-uponor-clean1` with an honest test plan
- [ ] Spec updated in this repo if protocol understanding changed
