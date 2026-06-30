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

## Firmware analysis (Path B)
The CPU flash was disassembled in [Ghidra](https://ghidra-sre.org/). Import `clean1.s19` directly (the `MotorolaHexLoader` places the bytes at their real addresses) with language `HCS08:BE:16:MC9S08GB60`. The image is sparse: two config bytes at `0x107F`/`0x1800`, main code `0x8000-0xFFFF`, the interrupt vectors at `0xFFD0-0xFFFF`, and the reset vector (`0xFFFE`) points at the entry `0x807F`. Auto-analysis recovers ~267 functions.

Peripheral driver locations:
* **SPI -> nRF9E5 radio (slave side):** `0xBD56` is the SPI **receive interrupt handler** (vector `0xFFE0` = `BD 56`), and peripheral init writes `SPI1C1=0xCC` (`MSTR=0`) — so the MC9S08 is the SPI **slave**. The nRF9E5's embedded 8051 is the master and owns the nRF905 `W_CONFIG` (channel / address / CRC). That config is therefore **not present in this flash dump**; recovering it needs an SDR capture or a dump of the nRF9E5's internal program memory. The `SPI1D` accesses at `0xBD5B-0xBDB1` are the slave's byte handling, not config setup.
* **I2C -> M24128 EEPROM (U3):** the bus driver is `0xDED1-0xE03A`. `FUN_ded1` is start + send-byte, `FUN_dfd2` reads a byte, `FUN_df6a` is stop, and `FUN_a554(buf, _, len, addrHi, addrLo)` is a generic sequential read of `len` bytes from a 16-bit word address. There are length-typed wrappers around it (1/2/4/5/16/23/25-byte reads).
* **Peripheral init:** `0xFE80` configures the SPI (`SPI1C1/C2/BR`) and I2C (`IIC1A/IIC1F`) registers; it runs from reset.

### Where the EEPROM data really lives
The firmware reads/writes records near the **top** of the M24128, e.g. `0x3A9D` (5-byte record) and `0x3AD5` (2-byte), plus stride-8 indexed record arrays. The `i2cdump` above only captured page 0 (`0x00-0xFF`), so it **misses the active region around `0x3A00-0x3FFF`**. A full 16-Kbit dump of that area is needed to see the live config/telemetry. (`i2cdump` only does single-byte addressing; use a small 2-byte-addressed read loop or the kernel `at24`/`eeprom` driver instead.)
