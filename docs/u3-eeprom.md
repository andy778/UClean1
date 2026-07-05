# U3 — M24128 I2C EEPROM

U3 is an [M24128](https://www.st.com/en/memories/m24128-bw.html) 128-Kbit (16 KB)
serial EEPROM on the Clean 1 PCB, marked `4128BWP 8424K`. It sits on the board's
I2C bus at address **`0x50`** and holds the firmware's config + telemetry
records. This page covers how to wire it to a Raspberry Pi, how to read it, and
what we have found so far.

> This dump is from **board A** (the retired original) — see the two-boards note
> in the [README](../README.md). Board B (the live unit) will hold different
> config.

## Wiring the Raspberry Pi to U3

The M24128 is an 8-pin device (SO8/DIP8). Standard pinout:

| Pin | Name      | Function                         |
| --- | ---       | ---                              |
| 1   | E0        | Chip-address bit 0               |
| 2   | E1        | Chip-address bit 1               |
| 3   | E2        | Chip-address bit 2               |
| 4   | VSS       | Ground                           |
| 5   | SDA       | I2C data                         |
| 6   | SCL       | I2C clock                        |
| 7   | WC        | Write control (low = writable)   |
| 8   | VCC       | +1.8 … 5.5 V supply              |

Because the chip answers at `0x50`, the three address pins **E0/E1/E2 are all
tied low** (the I2C device-type code `1010` + `E2 E1 E0 = 000` = `0x50`) — and on
this board they are grounded **by the PCB**, so they need no wiring from the Pi.

**What was actually connected for this dump:** just **four** wires, all on the
Pi's 40-pin header (I2C **bus 1**) — because the chip was read **in-circuit**, the
board itself provides E0/E1/E2 (→ `0x50`) and WC:

| Pi header pin | Pi function  | → M24128 pin |
| ---           | ---          | ---          |
| **pin 1**     | 3V3          | 8  VCC       |
| **pin 3**     | GPIO2 / SDA1 | 5  SDA       |
| **pin 5**     | GPIO3 / SCL1 | 6  SCL       |
| **pin 6**     | GND          | 4  VSS       |

All four sit in the **top-left corner** of the 40-pin header (odd pins in the
left column, even in the right; pin 1 is the square pad):

```
    3V3  ──▶ [ 1] [ 2]     5V
GPIO2/SDA ──▶ [ 3] [ 4]     5V
GPIO3/SCL ──▶ [ 5] [ 6] ◀── GND
              [ 7] [ 8]
```

Full interactive reference: **[pinout.xyz/pinout/i2c](https://pinout.xyz/pinout/i2c)**.

The chip's other pins were **left to the board**: E0/E1/E2 (pins 1/2/3) are held
low by PCB traces and WC (pin 7) is already tied — so a read-only in-circuit dump
needs nothing on them. (Reading a **desoldered** chip on a breadboard, you must
add them yourself: E0/E1/E2 → GND for address `0x50`, WC → GND or VCC.)

Notes:
- Bus 1 on the Pi has on-board ~1.8 kΩ pull-ups on SDA/SCL, so external
  pull-ups are usually unnecessary.
- Use **3V3, never 5V**, on a Pi — the GPIO is not 5V-tolerant. Pi pin 1 is 3V3.
- Enable I2C first: `sudo raspi-config` → Interface Options → I2C, then reboot.
- In-circuit means the CPU shares this bus — see bus-contention note below.

## Reading in-circuit vs. desoldered

The original dump showed "different results" between attempts. That is almost
certainly **bus contention**, not a bad chip: while the board is powered the
MC9S08 (U2) is also a master on this same I2C bus, so the Pi and the CPU fight
over it. Pick one of:

- hold the CPU in reset while the Pi reads, **or**
- power the EEPROM from the Pi with the rest of the board off, **or**
- desolder U3 (last resort) and read it on a breadboard / programmer.

## Confirm it is present

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

## The full 16 KB dump (do this)

`i2cdump` is **not enough**: it only reaches offset `0x00–0xFF` and sends a
single address byte, but the M24128 needs a **14-bit (2-byte) address**. The
firmware's live records live near `0x3A00–0x3FFF`, well outside `i2cdump`'s
reach (see "Where the data lives" below). Use the helper, which does correct
2-byte addressing:

```bash
tools/dump_eeprom.sh 1 dumps/u3-m24128-eeprom.bin
```

It prefers the kernel `at24` driver (clean binary) and falls back to chunked
`i2ctransfer`. Spot-check one known record:

```bash
i2ctransfer -y 1 w2@0x50 0x3a 0x9d r5     # 5 bytes at 0x3A9D
```

Deliverable: `dumps/u3-m24128-eeprom.bin`, exactly **16384 bytes**.

## What the old partial dump showed

`i2cdump` captured only page 0. The first ~17 bytes carry data; `XX` marks
bytes redacted here. This is **not** the interesting region — it is shown for
completeness:

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

## Where the data really lives

From the firmware analysis (see [ghidra-firmware-analysis.md](ghidra-firmware-analysis.md)), the MC9S08 reads/writes its
live records near the **top** of the chip — e.g. a 5-byte record at `0x3A9D`, a
2-byte record at `0x3AD5`, plus stride indexed record arrays. The page-0 dump
above misses all of it.

A full 16 KB dump now exists — [`dumps/u3-m24128-eeprom.bin`](../dumps/u3-m24128-eeprom.bin) — and
is decoded in **[docs/eeprom-map.md](eeprom-map.md)**. The `0x3A9D` region turned
out to be the GSM/SMS alert config (menu password + phone-number slots), and the
`0x0000–0x05BF` block is the static cycle/phase/alarm label table. There is **no
live telemetry** in the EEPROM.
