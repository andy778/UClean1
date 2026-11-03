# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repository is

This is a hardware reverse-engineering research project, **not** a software application. The goal is to get the Uponor Clean 1 (a wastewater treatment unit) integrated into Home Assistant. There is no build system, test suite, or application code — the repository documents findings and stores extracted firmware/memory artifacts plus small bench/analysis tools. Start at [README.md](README.md) and the table under "Where to look" for the current state and which doc covers what.

## Two telemetry routes — don't conflate them

- **Path A, serial/modem port (working, primary route for Home Assistant):** U2 runs a GSM/SMS controller over SCI2 (9600 8N1) to a Telit modem on the `Modem` header (RS-232 via U10/SP3232, **not** TTL). Impersonating the modem (no SIM needed) with [tools/fake_telit.py](tools/fake_telit.py) gets `CYCLE COUNTER`, `PLANT STATUS` (S-code), `ALARM STATUS` in plain ASCII. No heartbeat — must be polled. Full protocol in [docs/u2-serial-protocol.md](docs/u2-serial-protocol.md).
- **Path C, 868.2 MHz radio (zero-wiring complement):** passive RTL-SDR receive of the 5 panel alarm symbols/state only — no cycle counter or phase. Decoder spec in [docs/rtl433-decoder.md](docs/rtl433-decoder.md) and [docs/rtl433-implementation-spec.md](docs/rtl433-implementation-spec.md) (the latter is self-contained with test vectors — start there to build a decoder or TX in any language). The working decoder lives in a fork of [rtl_433](https://github.com/andy778/rtl_433/blob/add-uponor-clean1/src/devices/uponor_clean1.c), not in this repo.

Both are confirmed and complementary; don't treat one as superseding the other.

## Two physical boards — artifact provenance matters

**Board A** is the retired bench mule (failed OC13 output driver only — CPU/serial/EEPROM/radio intact). Every file in `dumps/` (`u2-mc9s08gt-flash.*`, `u3-m24128-eeprom.bin`) was read from board A. **Board B** is the current live replacement; every `rtl_433` radio capture and the display's cycle counter reading come from board B. They're the same model but not guaranteed byte-identical (EEPROM config, counter, radio pairing differ per unit) — check `SW Ver:`/`Proc Ver:` match before cross-using board-A firmware analysis to explain board-B radio behavior.

## Firmware analysis workflow (Ghidra)

Two separate CPU targets live inside the same flash dump:

- **U2, MC9S08GT (HCS08 core):** `tools/analyze.sh <Script.java> [args]` runs headless Ghidra against `dumps/u2-mc9s08gt-flash.s19` (loader `MotorolaHexLoader`, processor `HCS08:BE:16:MC9S08GB60`), using scripts in `tools/ghidra/`, and throws the Ghidra project away afterward (stateless/reproducible). Requires `GHIDRA_HOME` (default `/opt/ghidra`).
- **U1's nRF9E5 radio firmware (8051 core):** has no separate boot chip — it's embedded inside the U2 flash and served over SPI at boot. Extract it first (`dd` recipe in [docs/nrf9e5-firmware.md](docs/nrf9e5-firmware.md) and the header of `tools/analyze-8051.sh`) to `dumps/u1-nrf9e5-8051.bin`, then run `tools/analyze-8051.sh <Script.java> [args]` (loader `BinaryLoader`, processor `8051:BE:16:default`).

Recovered decompiled output and driver maps already live in `docs/ghidra/` (`mc9s08gt32_full.c`, `nrf9e5_full.c`, `codes.md`, `register-map.md`, `radio_frame.c`, `alarm_frames.c`, `crc16.c`) — check there before re-running analysis. [docs/ghidra-firmware-analysis.md](docs/ghidra-firmware-analysis.md) covers how the headless analysis was run and its gotchas.

## EEPROM (U3, I2C, address 0x50)

`i2cdump` only reaches the first 256 bytes and sends a single address byte — insufficient for the M24128's 14-bit addressing. Use `tools/dump_eeprom.sh [bus] [outfile]` on the Pi hosting the I2C bus instead; it prefers the kernel `at24` driver and falls back to chunked `i2ctransfer` reads to get the full 16 KB. Layout and the alarm/E-code table are in [docs/eeprom-map.md](docs/eeprom-map.md).

## Numbering spaces

The firmware has three separate, easily-confused numbering spaces — E-codes (alarm/fault), S-codes (plant status/phase), and radio message types. They're kept deliberately apart in [docs/ghidra/codes.md](docs/ghidra/codes.md); don't assume a numeric code means the same thing across the three.

## Conventions for this repo

- Docs are written in a neutral, third-person voice — no contributor names or first-person attribution in doc text itself; that lives in issue history and git blame, not in the prose.
- Every change, however small, goes through a branch + PR rather than a direct commit to `main`; sync `main` and delete the local branch after merge.
- Claims in docs are tagged by evidence strength (e.g. "firmware-confirmed", "silkscreen-confirmed", "static analysis, not yet confirmed on hardware") — preserve that distinction when editing rather than flattening to a single confidence level.
