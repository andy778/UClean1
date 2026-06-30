# Firmware analysis (Path B) — Ghidra

The U2 CPU is a Freescale/NXP **MC9S08GT32** (HCS08 core). Its flash was read
out with a [USBDM](https://sourceforge.net/projects/usbdm/files/) programmer to
`clean1.s19`, then disassembled in [Ghidra](https://ghidra-sre.org/). This page
records exactly how that was done so anyone can reproduce it, and what was
found. The raw decompiler output for the interesting drivers is checked in under
[`docs/ghidra/`](ghidra/).

## How the analysis was run

Everything runs **headless** (no GUI) through a small wrapper, so a run is
reproducible and leaves no project behind. The tooling lives in
[`tools/`](../tools/) (added in PR #50):

- `tools/analyze.sh` — imports `clean1.s19` and runs a Ghidra script.
- `tools/ghidra/DumpReport.java` — lists recovered functions + every SPI/IIC
  register access.
- `tools/ghidra/DecompileCallers.java` — decompiles the functions containing
  given addresses **plus their direct callers**, to a file.

### The two settings that matter

1. **Loader = `MotorolaHexLoader`.** The `-loader` argument takes the loader's
   *class name*, not its display name. `"Motorola Hex"`, `"Binary"`,
   `"Raw Binary"` are all rejected as invalid; the Intel-Hex auto-loader chokes
   on the S-records ("line does not start with record mark"). Importing the
   `.s19` (not the flat `.bin`) lets the loader place bytes at their real
   addresses.
2. **Processor = `HCS08:BE:16:MC9S08GB60`.** (The GB60 variant matches the
   GT32's core/peripherals closely enough for this work.)

The exact command `analyze.sh` issues:

```bash
"$GHIDRA/support/analyzeHeadless" "$PROJDIR" proj \
  -import clean1.s19 \
  -loader MotorolaHexLoader \
  -processor "HCS08:BE:16:MC9S08GB60" \
  -scriptPath tools/ghidra \
  -postScript "$SCRIPT" "$@" \
  -deleteProject
```

Set `GHIDRA_HOME` if Ghidra is not under `/opt/ghidra`.

### Commands actually used

```bash
# Function map + every SPI/IIC peripheral-register access:
tools/analyze.sh DumpReport.java

# Follow the EEPROM access layer outward (driver + callers) to a file:
tools/analyze.sh DecompileCallers.java eeprom_i2c.c a554 ded1 dfd2 df6a

# Peripheral init (settles the SPI master/slave question):
tools/analyze.sh DecompileCallers.java periph_init.c fe80
```

### Gotchas hit along the way

- **Scripts must be Java, not Python.** This Ghidra build was not started with
  PyGhidra, so Python post-scripts fail with "Python is not available". The
  scripts here are `GhidraScript` subclasses.
- **Write decompiler C to a file, not stdout.** Multi-line `println` output is
  mangled by Ghidra's per-line log prefixes; `DecompileCallers.java` uses a
  `PrintWriter` instead.
- **`0xBD56` does not decompile.** It sits in a code gap and is never turned
  into a function by auto-analysis, so `DecompileCallers` reports
  "(no function at 0xbd56)" and forcing disassembly there yields garbage. Its
  role is established from the vector table + init instead (see below).

## Image layout

The image is sparse:

| Region            | Address range   |
| ---               | ---             |
| Config bytes      | `0x107F`, `0x1800` |
| Main code         | `0x8000–0xFFFF` |
| Interrupt vectors | `0xFFD0–0xFFFF` |
| Reset entry       | `0xFFFE` → `0x807F` |

Auto-analysis recovers ~267 functions.

## Findings

### SPI → nRF9E5 radio (the MC9S08 is the *slave*)

`0xBD56` is the **SPI receive interrupt handler** — vector `0xFFE0` holds
`BD 56`. Peripheral init then writes `SPI1C1 = 0xCC`
([`docs/ghidra/periph_init.c`](ghidra/periph_init.c), the `SPI1C1 = 0xcc;`
line), and bit 4 of that value (`MSTR`) is **0** → the MC9S08 is the SPI
**slave**. So the nRF9E5's embedded 8051 is the SPI master and owns the nRF905
`W_CONFIG` (channel / address / CRC).

**Consequence:** the radio configuration is **not present in `clean1.s19`**.
Recovering it needs either an SDR capture of the 868.35 MHz link or a dump of
the nRF9E5's internal 8051 program memory. The `SPI1D` accesses around
`0xBD5B–0xBDB1` are the slave's byte handling, not config setup.

### I2C → M24128 (U3) EEPROM access layer

Bus driver region `0xDED1–0xE03A`. Full decompiled C:
[`docs/ghidra/eeprom_i2c.c`](ghidra/eeprom_i2c.c).

| Function | Role |
| ---      | ---  |
| `FUN_ded1` | I2C start + send byte |
| `FUN_dfd2` | read a byte |
| `FUN_df6a` | stop |
| `FUN_a554(buf, _, len, addrHi, addrLo)` | sequential read of `len` bytes from a 16-bit word address |

There are length-typed wrappers around `FUN_a554` — e.g. `FUN_a631` (2 bytes),
`FUN_a646` (4), `FUN_b596` (5), `FUN_bbe8` (16), `FUN_b5ab` (23), `FUN_b5c0`
(25) — each visible in the C calling `FUN_a554(..., <len>, ...)`.

### Peripheral init

`FUN_fe80` runs from reset and configures the clock (ICG), ports, SCI, the I2C
registers (`IIC1A/IIC1F/IIC1C`), and SPI (`SPI1C1/C2/BR`). Full C:
[`docs/ghidra/periph_init.c`](ghidra/periph_init.c).

### Where the EEPROM data really lives

The firmware reads/writes records near the **top** of the M24128 — e.g. `0x3A9D`
(5-byte record), `0x3AD5` (2-byte), plus stride-8 indexed record arrays. The
README `i2cdump` only captured page 0 (`0x00–0xFF`) with single-byte
addressing, so it **misses the active region around `0x3A00–0x3FFF`**. See
[docs/u3-eeprom.md](u3-eeprom.md) for the correct full 16 KB dump procedure.
