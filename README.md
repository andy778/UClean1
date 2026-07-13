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

## Modem port / serial (Path A) — **working**
This is now the **validated primary telemetry route**. The U2 CPU runs a full
**GSM/SMS controller**: it drives a Telit modem over SCI2 at 9600 8N1 and answers
a text command interface that reports the plant state in plain ASCII.

Impersonating that modem on the bench (no SIM) reads it directly — send the unit's
4-character access code and it replies:

```
CYCLE COUNTER:2129
PLANT STATUS:S102      (S102 = Aeration; S1xx clean / S2xx wait / S3xx maint / S4xx test)
ALARM STATUS:NO
```

The J5 "Modem" header is **RS-232 via U10/SP3232** (not TTL), which is why the
first attempt — a bare USB-serial cable sweeping baud rates — saw nothing: it was
reading inverted RS-232 levels, and with no modem attached U2 mostly stays quiet.
Tap the SP3232 TTL side (or use an RS-232 adapter), impersonate the modem, and the
telemetry falls out. Full write-up, wiring, command grammar, and the
[`tools/fake_telit.py`](tools/fake_telit.py) bench tool:
**[docs/u2-serial-protocol.md](docs/u2-serial-protocol.md)**.

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

The board carries a row of **seven identical output-driver channels** (the
repeated stages that switch the magnetventiler MV1–MV5, the compressor and the
pumps), so the designators fall into three matched series — one part per channel:
* **D6–D12** are all the same VNE46 mosfet as D11.
* **OC1–OC7** are all the same 1435 814 mosfet as OC6.
* **OC8–OC14** are all the same MOC3063 optoisolator as OC13.

The two OC series interleave across the row as two descending runs (…OC14, OC7,
OC13, OC6, OC12, OC5… reading left to right), read off the high-resolution
silkscreen in `docs/uclean1-pcb.png`.

**Confirmed by silkscreen** (a second board photo, terminal-block edge): the
output row's screw terminals are labelled, left to right, `MV1 Compressor`,
`MV2`, `MV3`, `MV4`, `MV5`, `Spare`, `Alarm` — matching
[docs/eeprom-map.md](docs/eeprom-map.md)'s EEPROM-derived fault table exactly
(`E040` compressor, `E041`–`E045` = MV1–MV5) and confirming `Spare` as a genuine
unused 7th channel, not a numbering gap. `Alarm` is a separate 2-pin terminal
(not part of the output row) — likely a dry-contact/relay output for an
external alarm siren or building-management input, not yet traced in firmware.

Same photo also shows, near U2: a 3-digit 7-segment display, a physical
push-button silkscreened **`Test`**, and a separate 4-pin header silkscreened
**`Test btn`** (for an external test button) — plus a `TEST 464 PASSED` QC
sticker. No matching string exists in the U2 flash (checked via `strings`), so
this is most likely a **factory test-jig interface** (assembly-line QC step),
not a field service feature — kept distinct from the still-open "handheld
service device" question in
[docs/ghidra/codes.md](docs/ghidra/codes.md#4-service-device--rf-no-pairing-not-ir-two-solid-leads-no-capture-yet).
Also visible: a gold SMA-style antenna connector next to U1, and the `Modem`
header from [docs/u2-serial-protocol.md](docs/u2-serial-protocol.md).

## I2C Memory (U3, 128-Kbit / 16 KB)
U3 is an M24128 EEPROM on the I2C bus at address `0x50`, read with a Raspberry Pi
and i2c-tools. Wiring the Pi to the chip, reading the full 16 KB (the `i2cdump`
above only reaches the first 256 bytes), and the dump analysis now live on their
own page: **[docs/u3-eeprom.md](docs/u3-eeprom.md)**.

## Flash content
To read out the flash content from the CPU using reader like [USBDM](https://sourceforge.net/projects/usbdm/files/) will get you and srecord [dumps/u2-mc9s08gt-flash.s19](dumps/u2-mc9s08gt-flash.s19) file. 
This file one need to convert to binary file [dumps/u2-mc9s08gt-flash.bin](dumps/u2-mc9s08gt-flash.bin) with e.g objcopy.

```
objcopy --input-target=srec --output-target=binary dumps/u2-mc9s08gt-flash.s19 dumps/u2-mc9s08gt-flash.bin
```

### Reading the flash back out (BDM dump vs. security)

Reading the flash is a **debug** operation, not a *programming* one. A plain flash
programmer only exposes the program flow (blank-check → erase → program → verify)
and has no "read". To dump memory you use the BDM debug channel and a tool that
issues the BDM memory-read command:

- **USBDM + GDB server** — start the HCS08 GDB server that ships with USBDM, then in
  the HCS08 `gdb`:

  ```
  target remote localhost:1234
  dump srec memory flash.s19 0x8000 0x10000   # 32 KB flash of the GT32
  dump srec memory ram.s19   0x0080 0x1080    # RAM, optional
  ```

  `dump srec memory FILE START END` walks the range over BDM and writes an S-record
  (use `dump ihex memory …` for Intel-hex). MC9S08GT32 map: 32 KB flash at
  **0x8000–0xFFFF**, RAM at 0x0080–0x107F.
- **CodeWarrior / P&E Multilink** — connect in the debugger and use *Import/Export
  Memory → Export* (or the `save` command) over the flash range.

**Flash security caveat.** If read-back is blocked, the part is *secured*: the FSEC
byte at **0xFFBF** (SEC01:SEC00) makes a programmer able only to mass-erase, never
read. When secured, BDM reads of flash and RAM are blocked and the only legal
operation is a mass-erase (which unsecures by wiping everything — useless for
dumping). The one non-destructive escape is the 8-byte **backdoor key** at
0xFFB0–0xFFB7, if the firmware enabled KEYEN and you know it. The existing
`dumps/u2-mc9s08gt-flash.s19` came out cleanly, so **U2 on this board was not
secured** — a straight BDM dump is enough.

## Firmware analysis (Path B)
The U2 CPU flash (`dumps/u2-mc9s08gt-flash.s19`) was disassembled in [Ghidra](https://ghidra-sre.org/). How the headless analysis was run, the gotchas, and the recovered driver map (I2C EEPROM access + the SPI-slave finding) now live on their own page: **[docs/ghidra-firmware-analysis.md](docs/ghidra-firmware-analysis.md)**.

## nRF9E5 firmware (Path C)
U1's radio firmware (8051, on-air Manchester/frame/nRF905 config) has **no external boot chip** — it's embedded in the U2 flash we already dumped, and has been extracted + disassembled. See **[docs/nrf9e5-firmware.md](docs/nrf9e5-firmware.md)**.

## Radio decoding (rtl_433)
The 868 MHz frame (address, 32-byte payload, CRC-16) is confirmed and the decoder is **enabled and CRC-gated** in the fork: [andy778/rtl_433, branch `add-uponor-clean1`](https://github.com/andy778/rtl_433/blob/add-uponor-clean1/src/devices/uponor_clean1.c). Spec, byte mapping, capture recipes, and what's still open: **[docs/rtl433-decoder.md](docs/rtl433-decoder.md)**.
