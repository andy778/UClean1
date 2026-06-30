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

## I2C Memory (128-Kbit alias 16kb)
Extracted with help of a raspberry pi and i2c-tools, Memory exist at address 0X50 as can be seen from below. 

Note! I think one need to desolder the memory chips as there was different results.   
```
i2cdetect -y 1       
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:                         -- -- -- -- -- -- -- -- 
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
50: 50 -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
60: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
70: -- -- -- -- -- -- -- --                         
```

Seems to be 17 first bytes that contain some data
```
i2cdump -y 1 0x50
No size specified (using byte-data access)
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f    0123456789abcdef
00: 05 00 69 00 6d 00 67 00 00 01 20 00 79 00 00 6f    ?.i.m.g..? .y..o
10: 00 XX XX XX XX XX 00 XX ff XX XX XX ff XX XX XX    .XXXXX.X.XXX.XXX
20: XX XX XX XX XX ff ff XX ff ff ff ff ff ff ff ff    XXXXX..X........
30: ff ff ff ff ff ff ff ff ff ff ff XX ff XX XX XX    ...........X.XXX
40: ff XX XX XX XX XX XX XX XX ff XX XX XX XX XX ff    .XXXXXXXX.XXXXX.
50: XX XX XX XX XX XX XX XX XX XX ff ff ff ff ff ff    XXXXXXXXXX......
60: ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff    ................
70: ff ff ff ff ff ff ff ff XX XX ff XX XX XX XX XX    ........XX.XXXXX
80: XX XX XX XX XX ff ff ff ff ff ff ff ff ff ff ff    XXXXX...........
90: ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff    ................
a0: ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff    ................
b0: ff ff ff ff ff ff XX XX ff ff ff ff ff ff ff ff    ......XX........
c0: ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff    ................
d0: ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff    ................
e0: ff ff ff ff ff ff ff ff ff ff 00 ff ff ff ff ff    ................
f0: ff ff XX XX XX XX XX XX XX XX XX XX XX ff ff ff    ..XXXXXXXXXXX...
```

## Flash content
To read out the flash content from the CPU using reader like [USBDM](https://sourceforge.net/projects/usbdm/files/) will get you and srecord [clean1.s19](clean1.s19) file. 
This file one need to convert to binary file [clean1.bin](clean1.bin) with e.g objcopy.

```
objcopy --input-target=srec --output-target=binary clean1.s19 clean1.bin
```

## Firmware analysis (Path B)
The CPU flash was disassembled in [Ghidra](https://ghidra-sre.org/). Import `clean1.s19` directly (the `MotorolaHexLoader` places the bytes at their real addresses) with language `HCS08:BE:16:MC9S08GB60`. The image is sparse: two config bytes at `0x107F`/`0x1800`, main code `0x8000-0xFFFF`, the interrupt vectors at `0xFFD0-0xFFFF`, and the reset vector (`0xFFFE`) points at the entry `0x807F`. Auto-analysis recovers ~267 functions.

Peripheral driver locations:
* **SPI -> nRF9E5 radio:** the SPI byte-shift primitive is around `0xBD56`; writes to the SPI data register cluster at `0xBD5B-0xBDB1`. This is where the nRF905 `W_CONFIG` payload (channel / address / CRC) is set up.
* **I2C -> M24128 EEPROM (U3):** the bus driver is `0xDED1-0xE03A`. `FUN_ded1` is start + send-byte, `FUN_dfd2` reads a byte, `FUN_df6a` is stop, and `FUN_a554(buf, _, len, addrHi, addrLo)` is a generic sequential read of `len` bytes from a 16-bit word address. There are length-typed wrappers around it (1/2/4/5/16/23/25-byte reads).
* **Peripheral init:** `0xFE80` configures the SPI (`SPI1C1/C2/BR`) and I2C (`IIC1A/IIC1F`) registers; it runs from reset.

### Where the EEPROM data really lives
The firmware reads/writes records near the **top** of the M24128, e.g. `0x3A9D` (5-byte record) and `0x3AD5` (2-byte), plus stride-8 indexed record arrays. The `i2cdump` above only captured page 0 (`0x00-0xFF`), so it **misses the active region around `0x3A00-0x3FFF`**. A full 16-Kbit dump of that area is needed to see the live config/telemetry. (`i2cdump` only does single-byte addressing; use a small 2-byte-addressed read loop or the kernel `at24`/`eeprom` driver instead.)
