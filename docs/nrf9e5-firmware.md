# nRF9E5 firmware (Path C) — recovered from the U2 flash

**Result:** the nRF9E5's 8051 firmware — Manchester coding, frame header, payload
packing, and the nRF905 `W_CONFIG` (channel/address/CRC) — is **embedded in the
U2 flash we already have**, served to the nRF9E5 by U2 at power-on. Extracted to
[`dumps/u1-nrf9e5-8051.bin`](../dumps/u1-nrf9e5-8051.bin) (1648 bytes).

There is no external boot EEPROM on this board.

## Why: U2 *is* the nRF9E5's boot memory

U1 (nRF9E5 = nRF905 + 8051) has 4 KB program RAM and no on-chip non-volatile
memory — at every reset its boot ROM copies the program in over SPI, with the
nRF9E5 as bus master. Normally that program sits in a small external SPI
EEPROM; **this board has none**. Instead the nRF9E5's SPI lines run to **U2**
(the MC9S08), and the holes by the silkscreen **`BKGD`** text are U2's BDM debug
header — the same port its flash was dumped through. So U2 acts as an
SPI-slave EEPROM emulator and clocks the 8051 image into the radio at boot.

Matches the Ghidra finding that U2 is the SPI **slave** (`SPI1C1=0xcc`, `MSTR=0`
— [ghidra-firmware-analysis.md](ghidra-firmware-analysis.md)). The same SPI
link then carries runtime MC9S08↔8051 messages after boot.

## Where the image is

| | |
| --- | --- |
| flash address | `0x80bb` – `0x872b` |
| offset in `u2-mc9s08gt-flash.bin` | `0x703c` – `0x76ac` |
| length | 1648 bytes |

Identified by its 8051 interrupt vector table at the start (`02 00 2b` =
`LJMP 0x2B` reset vector, etc. — all 8051 opcodes, none HCS08). Ends at its
final `RET`, right before the HCS08 string table (`Dosing Cnt`, …). *(An
earlier pass mislabeled the leading bytes as a "7-byte init message" — they're
the reset vector.)*

```bash
dd if=dumps/u2-mc9s08gt-flash.bin of=dumps/u1-nrf9e5-8051.bin \
   bs=1 skip=$((0x703c)) count=$((0x670))
```

Disassemble with **[`tools/analyze-8051.sh`](../tools/analyze-8051.sh)
`Dump8051.java`** (headless Ghidra, `8051:BE:16:default`, base `0x0000`).

## nRF905 radio configuration (confirmed)

The config-write routine at `0x015c` clocks out `W_CONFIG` + a 10-byte
`RF_CONFIG` literal: `75 06 44 20 20 EA EA EA EA D4`.

| Field | Value | Meaning |
| ---   | ---   | --- |
| `CH_NO` / `HFREQ_PLL` | `0x75`=117 / 1 | **868.2 MHz** — matches the measured carrier |
| `TX_AFW`/`RX_AFW` | 4 | **4-byte address** |
| `RX_PW`/`TX_PW` | `0x20` | **32-byte payload** |
| `RX_ADDRESS` | `EA EA EA EA` | (`W_TX_ADDRESS` sets the same for TX) |
| `CRC_EN`/`CRC_MODE` | 1/1 | **CRC-16 enabled** |

All of this is independently confirmed on-air in
[docs/rtl433-decoder.md](rtl433-decoder.md).

## The 8051 is a transparent relay

The RX handler at `0x01c8` reads 32 bytes (`R_RX_PAYLOAD`); TX (`0x030b`)
clocks out a 32-byte buffer from RAM `0x30–0x4F`. That buffer is filled from
the 8051's serial ISR (`0x029f`) — bytes it receives from the MC9S08. Tracing
the RX processor (`0x03dc`):

| byte | action |
| ---  | ---    |
| `0x01`–`0x1f` | length N → next N bytes are the radio payload, then TX |
| `0x55`/`0x56` | radio disable/enable |
| `0x73` | radio register (config) access |
| `0x74`/`0x75` | set/clear a flag |
| `0x76` | begin a multi-byte value load |
| `0xc0` | report 9 bytes back to U2 |

**The 8051 never computes payload content — U2 supplies it byte-for-byte.** So
payload field semantics live in the MC9S08 firmware, not here (see the byte
mapping already found in [rtl433-decoder.md](rtl433-decoder.md)).

Also settles an earlier open question: this relay path copies bytes **raw** —
no software Manchester encoder here or on the MC9S08 side. The on-air
Manchester coding is therefore a radio/PHY-layer effect, not a firmware step.

## Optional: confirm the boot-serve on the bench

Put a logic analyzer on the SPI between U2 and the nRF9E5 (`SCK`/`MOSI`/`MISO`
+ nRF9E5 chip-select), power-cycle, and watch the nRF9E5 master-read these same
1648 bytes out of U2 at reset.
