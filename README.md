# Uponor Clean 1

<!-- 
SPDX-License-Identifier: MIT
-->
<!--
[![REUSE status](https://api.reuse.software/badge/git.fsfe.org/reuse/api)](https://api.reuse.software/info/git.fsfe.org/reuse/api)
-->
[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/andy778/UClean1/badge)](https://securityscorecards.dev/viewer/?uri=github.com/andy778/UClean1)



Investigate if its possible to get Uponor Clean 1 into Home Assistant

## Hypothesis
* From the modem port connect an ESPHOME (e.g ESP32 S3) and insert values into Home Assistant
* It's 868.35MHz radio between inner unit and outer unit, and make decoding of this device in [rtl_433](https://github.com/merbanan/rtl_433/)  

![PCB with text](docs/uclean1-pcb.png)

## Two boards (dump vs. radio provenance)
This project uses **two physical Clean 1 boards**, and it matters which artifact
came from which:
* **Board A** — the original, retired after **OC13** (a MOC3063 output-driver
  optotriac, one of the OC8–OC14 channels) failed. **All firmware and memory
  dumps in [`dumps/`](dumps/) were read from board A** (`u2-mc9s08gt-flash.*`,
  `u3-m24128-eeprom.bin`). OC13 is only an output stage, so its CPU / serial /
  EEPROM / radio are intact — board A is the safe **bench mule**.
* **Board B** — the new **active replacement**, currently installed. **All
  868 MHz `rtl_433` radio captures come from board B**, as does the display's
  cycle counter (*satsräknare*).

So firmware/EEPROM analysis describes **board A** and on-air behaviour describes
**board B**. They are the same model but not guaranteed byte-identical — the
EEPROM config (code + phone numbers) and the counter differ per board, and the
radios may be individually paired. Confirm `SW Ver:` / `Proc Ver:` match on both
before using board-A firmware to explain board-B radio.

## Datasheets
| No       | Description | IC           |
| ---      | ---         |---           |
| U1       | Transceiver | [nRF9E5](https://web.archive.org/web/20240428224643/https://infocenter.nordicsemi.com/pdf/nRF9E5_PS_v1.6.pdf) |
| U2       | CPU         | [MC9S08GT32ACFBE](https://www.nxp.com/docs/en/data-sheet/MC9S08GB60A.pdf)  |
| U10      | Serial      | [sipex 3232](https://www.silicon-ark.co.uk/datasheets/sp3222_3232e-datasheet-sipex.pdf)   |
| U3       | I2C Mem     | [4128BWP 8424K](https://www.st.com/en/memories/m24128-bw.html) |
| OC8–OC14 | Optoisolator| MOC3063 1512 |
| D6–D12   | Mosfet      | VNE46 AC 4DLMG |
| OC1–OC7  | Mosfet      | 1435 814 |

## I2C Memory (U3, 128-Kbit / 16 KB)
U3 is an M24128 EEPROM on the I2C bus at address `0x50`, read with a Raspberry Pi
and i2c-tools. Wiring the Pi to the chip, reading the full 16 KB (the `i2cdump`
above only reaches the first 256 bytes), and the dump analysis now live on their
own page: **[docs/u3-eeprom.md](docs/u3-eeprom.md)**.

## Flash content
The CPU flash was dumped via BDM to [`dumps/u2-mc9s08gt-flash.s19`](dumps/u2-mc9s08gt-flash.s19) and converted to binary with `objcopy`. Full Ghidra analysis lives on its own page: **[docs/ghidra-firmware-analysis.md](docs/ghidra-firmware-analysis.md)**.

```
objcopy --input-target=srec --output-target=binary dumps/u2-mc9s08gt-flash.s19 dumps/u2-mc9s08gt-flash.bin
```

## Serial / modem port (Path A)
The `CODE` command interface over the modem port was validated end-to-end via a fake-modem bench test — see **[docs/u2-serial-protocol.md](docs/u2-serial-protocol.md)**.

## nRF9E5 firmware extraction (Path C)
The on-air radio format lives in the nRF9E5's embedded 8051, which has no internal non-volatile memory and boots its code from an external SPI memory. The plan for recovering that 8051 image lives on its own page: **[docs/nrf9e5-firmware.md](docs/nrf9e5-firmware.md)**. This is bench work not yet done — no nRF9E5 dump is checked in.

## Radio decoding (rtl_433)
The 868.35 MHz link between inner and outer units is being decoded for [rtl_433](https://github.com/merbanan/rtl_433/). Decoder spec and build instructions: **[docs/rtl433-decoder.md](docs/rtl433-decoder.md)**. Capture findings and frame dumps: **[docs/radio-capture-log.md](docs/radio-capture-log.md)**.
