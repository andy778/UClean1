# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repository is

This is a hardware reverse-engineering research project, **not** a software application. The goal is to investigate whether the Uponor Clean 1 (a wastewater treatment unit) can be integrated into Home Assistant. There is no build system, test suite, or application code — the repository documents findings and stores extracted firmware/memory artifacts.

## Working with the firmware artifacts

Convert the S-record dump to a flat binary (the conversion documented in the README):

```
objcopy --input-target=srec --output-target=binary dumps/u2-mc9s08gt-flash.s19 dumps/u2-mc9s08gt-flash.bin
```

The CPU is a Freescale/NXP MC9S08GT (HCS08 core). The radio link between inner and outer units is 868.2 MHz (channel 117); the eventual decoding target is [rtl_433](https://github.com/merbanan/rtl_433/). The I2C EEPROM (M24128, 128-Kbit) lives at address `0x50`; in the README dump, `XX` marks bytes redacted/unknown and meaningful data appears in roughly the first ~17 bytes.
