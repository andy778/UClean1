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

![PCB with text](uclean1.png)

## Modem port / serial (Path A)
Attempted to read the modem port directly with a laptop and a USB-to-serial cable, sweeping the common baud rates. No traffic was observed on any of them — the port appeared dead. This avenue is parked for now in favour of the flash/firmware work below (Path B).

## Datasheets 
| No       | Description | IC           |
| ---      | ---         |---           |
| U1       | Transceiver | [nRF9E5](https://docs.nordicsemi.com/bundle/nRF9-Series/resource/nRF9E5_PS_v1.6.pdf) |
| U2       | CPU         | [MC9S08GT32ACFBE](https://www.nxp.com/docs/en/data-sheet/MC9S08GB60A.pdf)  |
| U10      | Serial      | [sipex 3232](https://www.silicon-ark.co.uk/datasheets/sp3222_3232e-datasheet-sipex.pdf)   |
| U3       | I2C Mem     | [4128BWP 8424K](https://www.st.com/en/memories/m24128-bw.html) |
| OC13     | Optoisolator| MOC3063 1512 |
| D11      | Mosfet      | VNE46 AC 4DLMG |
| OC6      | Mosfet      | 1435 814 |

## I2C Memory (U3, 128-Kbit / 16 KB)
U3 is an M24128 EEPROM on the I2C bus at address `0x50`, read with a Raspberry Pi
and i2c-tools. Wiring the Pi to the chip, reading the full 16 KB (the `i2cdump`
above only reaches the first 256 bytes), and the dump analysis now live on their
own page: **[docs/u3-eeprom.md](docs/u3-eeprom.md)**.

## Flash content
To read out the flash content from the CPU using reader like [USBDM](https://sourceforge.net/projects/usbdm/files/) will get you and srecord [clean1.s19](clean1.s19) file. 
This file one need to convert to binary file [clean1.bin](clean1.bin) with e.g objcopy.

```
objcopy --input-target=srec --output-target=binary clean1.s19 clean1.bin
```
