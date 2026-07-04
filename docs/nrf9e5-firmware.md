# nRF9E5 firmware extraction (Path C)

The MC9S08 flash (Path B) does **not** contain the on-air radio format: that CPU
is only an SPI *slave* on a private MC9S08↔nRF9E5 message bus, so the Manchester
encoding, the `fd 7a ba ba ba 83` header, the payload packing, and the nRF905
`W_CONFIG` (channel / address / CRC) all live in the **nRF9E5's embedded 8051**,
not in `dumps/u2-mc9s08gt-flash.s19`. This page is the plan for getting that
8051 code out. It is bench work that has **not been done yet** — no nRF9E5 dump
is checked in.

## Why you can't just "read the chip"

U1 is a Nordic **nRF9E5** = nRF905 transceiver + an 8051 MCU. The 8051 has only
**4 KB of program RAM** and **no on-chip non-volatile memory** — it cannot retain
code across a power cycle. At every reset an internal boot ROM copies the program
into that RAM **from an external serial memory over SPI** (the nRF9E5 drives the
bus as master, selecting the memory with its `EECSN` chip-select), then runs it.

So there is nothing persistent *inside* the chip to read out the way the MC9S08
flash was read. "Dumping the nRF9E5 firmware" means capturing the code that lives
in — or streams out of — that external boot memory.

Bus roles are consistent with this: the Ghidra analysis found the MC9S08 is the
SPI slave (`SPI1C1 = 0xcc`, `MSTR = 0` — see
[ghidra-firmware-analysis.md](ghidra-firmware-analysis.md)). The nRF9E5 is the
SPI master on the shared bus, both when reading its boot memory and when talking
to the MC9S08.

## What the PCB photo shows

Reading [`docs/uclean1-pcb.png`](uclean1-pcb.png):

- **U1 (nRF9E5)** is on the *main* board, marked `NRF 9E5 1438BS`. Beside it:
  crystal **Y1**, the RF matching network, and the SMA antenna feed. **No 8-pin
  memory chip sits next to U1** — only a crystal and passives.
- The tilted board across the top is the **antenna board**, fed by the SMA on the
  main board. It is edge-on in the photo and appears passive (antenna trace only,
  no active parts) — not a carrier for a boot EEPROM.
- The only serial-memory-style **8-pin SOIC** visible is a black part near **U2**
  (marking approx. `8Z 768L / EA5`, flanked by C16/C17). The middle-right row of
  identical SOICs are the output/gate drivers for the 8 screw-terminal channels,
  not memory.

Open question this raises: that single 8-pin SOIC is the prime suspect for the
boot memory, **but it may instead be U3** (the M24128, which is I2C and already
dumped — see [u3-eeprom.md](u3-eeprom.md)). Resolve it on the bench before
assuming anything (next section).

## Two ways to get the 8051 image

### 1. Read the boot memory directly

First identify it with a multimeter, powered off. Buzz out the suspect 8-pin
SOIC's pins against the nRF9E5:

- If they land on nRF9E5 **SCK / MOSI / MISO + a chip-select (`EECSN`)** → this is
  the boot store. Read it in-circuit or desoldered with a cheap SPI programmer
  (CH341A + `flashrom`, or a Raspberry Pi). This is the direct analog of the
  USBDM read done for U2.
- If they land on **SDA / SCL** at the MC9S08 → it is just U3 (I2C), and the
  nRF9E5's boot memory is a part not yet identified. Fall back to method 2, which
  finds it regardless.

### 2. Sniff the power-on boot load (recommended)

This sidesteps having to visually identify or desolder anything. At reset the
nRF9E5 masters the bus and streams its **entire** program (≤4 KB) in from the
boot memory exactly once. Put a logic analyzer (even a low-cost Saleae clone) on
the nRF9E5's **SCK / MOSI / MISO / EECSN** pins, power-cycle, and capture the
boot sequence. Reconstruct the 8051 image from the SPI transaction — and as a
side effect you learn which chip sourced it.

## Bench setup — wiring & software

### Step 0 — identify the pins (both methods)

The boot memory is almost certainly a standard **25-series SPI EEPROM/flash** in
an 8-pin SOIC, which has a fixed pinout — so once you find the chip you know its
pins:

| SOIC pin | 25-series name | Role                     |
| ---      | ---            | ---                      |
| 1        | CS#            | chip select (= `EECSN`)  |
| 2        | SO / MISO      | data **out** (the code)  |
| 3        | WP#            | write-protect → tie 3V3  |
| 4        | VSS            | GND                      |
| 5        | SI / MOSI      | data **in** (cmd/addr)   |
| 6        | SCK            | clock                    |
| 7        | HOLD#          | → tie 3V3                |
| 8        | VCC            | **3.3 V** supply         |

Power **off**, on continuity/diode mode confirm pins 1/2/5/6 reach the nRF9E5's
`EECSN` / `MISO` / `MOSI` / `SCK`. If they instead land on the MC9S08's SDA/SCL,
that SOIC is just U3 (I2C) — method 2 still finds the real one.

> ⚠️ This is a **3.3 V** part — never put 5 V on it. Many CH341A programmers ship
> at 5 V; use a 3.3 V-modified one (or a level shifter), or you will damage the
> chip and back-feed the board.

### Method 2 — logic-analyzer sniff (recommended, no desolder)

Kit: any cheap 8-channel logic analyzer (an FX2 "Saleae clone", ~US$10) plus
**[PulseView / sigrok](https://sigrok.org/)** (free) or Saleae Logic.

Five clip leads (probe at the memory pins, or the same nets at the nRF9E5):

| LA channel | Signal          | 25-series pin |
| ---        | ---             | ---           |
| D0         | CS# / `EECSN`   | 1             |
| D1         | SCK             | 6             |
| D2         | MOSI (cmd/addr) | 5             |
| D3         | MISO (the code) | 2             |
| GND        | board GND       | 4             |

Capture:
1. Sample at **≥ 24 MS/s** (≥ 4× the SPI clock; a clone's ~24 MS/s is plenty for a
   boot clock of a few MHz).
2. Trigger on **D0 (CS#) falling edge**, arm the capture.
3. **Power-cycle the board.** At reset the nRF9E5 masters the bus and streams the
   **entire** ≤ 4 KB image in exactly once.
4. Add sigrok's **SPI decoder** (CS = D0 active-low, CLK = D1, MOSI = D2,
   MISO = D3; try **mode 0** = CPOL 0 / CPHA 0 first). The loader sends a read
   opcode (`0x03`) + address on MOSI, then the memory returns the image on
   **MISO** — export that MISO byte stream to `dumps/u1-nrf9e5-8051.bin`.

### Method 1 — direct read with an SPI programmer

Kit: a **CH341A** USB programmer (3.3 V!) with a SOIC-8 test clip, or a Raspberry
Pi on `spidev`, plus **[flashrom](https://www.flashrom.org/)**.

The chip shares the bus with the nRF9E5, so keep the nRF9E5 from driving it:
- **best:** desolder the SOIC and read it in the clip / on a breadboard, or
- clip it in place **with the board unpowered** (and the nRF9E5 held in reset).

Wire the clip to the 25-series pinout above:

| Programmer | → 25-series pin              |
| ---        | ---                          |
| CS         | 1 CS#                        |
| MISO / DO  | 2 SO                         |
| 3V3        | 8 VCC, plus 3 WP# and 7 HOLD# |
| GND        | 4 VSS                        |
| MOSI / DI  | 5 SI                         |
| CLK        | 6 SCK                        |

Read it:

```bash
flashrom -p ch341a_spi -r dumps/u1-nrf9e5-8051.bin           # CH341A
# or on a Raspberry Pi:
flashrom -p linux_spi:dev=/dev/spidev0.0,spispeed=1000 -r dumps/u1-nrf9e5-8051.bin
```

If flashrom can't auto-detect it, pass `-c <model>` (see `flashrom -L`) or read it
as a generic 25xx part. The image is ≤ 4 KB (the 8051's RAM size).

## After you have the image

The dump is **8051 machine code**. Disassemble it in Ghidra (it ships an 8051
processor module) or with SDCC tooling. That is where to recover:

- the software **Manchester** encoder (confirms the raw `01→1` / `10→0`
  convention the rtl_433 decoder assumes),
- the constant **`fd 7a ba ba ba 83`** header construction,
- the **payload byte packing** (would help resolve `[U5]`/`[U6]` in
  [rtl433.md](rtl433.md)),
- the nRF905 **`W_CONFIG`** write — RF channel, device address, and **CRC mode** —
  which closes the `[U4]` CRC question.

Store any resulting dump under `dumps/` following the existing
`<designator>-<part>-<kind>` convention, e.g. `dumps/u1-nrf9e5-8051.bin`.
