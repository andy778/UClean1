# nRF9E5 firmware (Path C) — recovered from the U2 flash

**Result:** the nRF9E5's 8051 firmware — the code that owns the on-air radio
format (software Manchester coding, the `fd 7a ba ba ba 83` header, payload
packing, and the nRF905 `W_CONFIG` for channel / address / CRC) — is **embedded in
the U2 flash we already have**. It is served to the nRF9E5 by U2 at power-on, not
stored in a separate memory chip. It has been extracted to
[`dumps/u1-nrf9e5-8051.bin`](../dumps/u1-nrf9e5-8051.bin) (1648 bytes).

This supersedes the earlier assumption that the radio code lived in an external
boot EEPROM that had to be read or sniffed — there is no such chip on this board.

## The architecture: U2 *is* the nRF9E5's boot memory

U1 is a Nordic **nRF9E5** = nRF905 transceiver + an 8051 MCU. The 8051 has 4 KB of
program RAM and **no on-chip non-volatile memory** — at every reset its boot ROM
copies the program in over SPI, with the nRF9E5 as bus **master**, then runs it.

On a typical design that program sits in a small external SPI EEPROM. **This board
has no such chip.** There is no serial-memory part next to U1; instead the
nRF9E5's SPI lines run to **U2** (the MC9S08), and the debug/programming holes by
the silkscreen **`BKGD`** text are the MC9S08 background-debug (BDM) header — the
same port U2's flash was dumped through. So **U2 serves the nRF9E5 its boot
image**: the MC9S08 acts as an SPI-slave EEPROM emulator and clocks the 8051
program into the radio at power-on.

This matches the firmware analysis: Ghidra found U2 is the SPI **slave**
(`SPI1C1 = 0xcc`, `MSTR = 0` — see
[ghidra-firmware-analysis.md](ghidra-firmware-analysis.md)), i.e. it responds to
the nRF9E5 master. The *same* SPI link then carries the runtime MC9S08↔8051
telemetry messages after boot.

## Where the image is, and how it was found

The 8051 image is a contiguous blob inside `dumps/u2-mc9s08gt-flash.bin`:

| | |
| --- | --- |
| flash address | `0x80bb` – `0x872b` |
| offset in the `.bin` | `0x703c` – `0x76ac` |
| length | 1648 bytes (`0x670`) |

It was identified by its **8051 interrupt vector table** at the start (8051
address `0x0000`):

```
0x0000:  02 00 2b   LJMP 0x002B    ; reset
0x000B:  02 02 56   LJMP 0x0256    ; timer 0
0x0023:  02 02 9f   LJMP 0x029F    ; serial / SPI
0x002B:  12 00 cb   LCALL 0x00CB   ; reset target -> init, then the
                                   ; e4 93 f2 a3 08 b8 ... MOVC/MOVX
                                   ; memory-clear + copy startup loop
```

`02` = `LJMP`, `12` = `LCALL`, `93` = `MOVC A,@A+DPTR`, `f2` = `MOVX @R0,A` — all
8051, none of them HCS08. The blob ends at its final `22` (`RET`), immediately
before the HCS08 string table (`Dosing Cnt`, `P-in-mix On`, …). *(An earlier Ghidra
pass mislabeled the leading `02 00 2b …` bytes as a "7-byte init message"; they are
the 8051 reset vector.)*

Extract it straight from the dump:

```bash
dd if=dumps/u2-mc9s08gt-flash.bin of=dumps/u1-nrf9e5-8051.bin \
   bs=1 skip=$((0x703c)) count=$((0x670))
```

(already checked in as [`dumps/u1-nrf9e5-8051.bin`](../dumps/u1-nrf9e5-8051.bin)).

## Disassemble it (8051)

Load `dumps/u1-nrf9e5-8051.bin` in Ghidra with the **8051** processor
(`8051:BE:16:default`), image base `0x0000`, and let it follow the vector table;
or use SDCC's `sdas` / `d52`. Targets to recover:

- the software **Manchester** encoder (confirms the `01→1` / `10→0` convention the
  rtl_433 decoder assumes),
- the **`fd 7a ba ba ba 83`** header construction,
- the **payload byte packing** (resolves `[U5]` / `[U6]` in [rtl433.md](rtl433.md)),
- the nRF905 **`W_CONFIG`** write — RF channel, device address, and **CRC mode** —
  which closes the `[U4]` CRC question.

Because this is board **A**'s image, confirm the `SW Ver:` / `Proc Ver:` match
board B before using it to explain board-B on-air captures (see the two-boards
note in the [README](../README.md)).

## First-pass disassembly

Loaded headless in Ghidra (`8051:BE:16:default`, base `0x0000`) — **777
instructions, code `0x0000`–`0x066f`** (the extract is code-complete; a small
initialized-data segment that crt0 copies from code `0x679` sits just past it).
Structure:

- **Reset `0x0000` → `0x2B`:** `MOV SP,#0x7F`, then a standard crt0 (clear IRAM /
  XRAM, copy XINIT from `0x679`) into the main loop at `0x26`.
- **ISRs:** timer 0 at `0x0256`, serial/SPI at `0x029F` (the vectors at `0x0B`
  and `0x23`).
- **Peripheral init `0x0120`:** `TMOD=0x01`, Timer0 reload `TH0:TL0 = 0x8AD0`,
  `SCON=0x50` (UART mode 1) — plus writes to the **nRF9E5 radio SFRs**
  `0x90`/`0x93`/`0x94`/`0x95`/`0x96`/`0x97` and `0x98`.

## The nRF905 radio configuration — read from the firmware

The config-write routine at `0x015c` selects the RF core (`SPI_CTRL=0x02`),
asserts the radio chip-select (`CLR P2.3`), then clocks out `W_CONFIG` (`0x00`)
followed by the 10-byte RF_CONFIG via `SPI_DATA` (`0xB2`, send routine `0x02fb`).
The bytes are literals in the code:

```
W_CONFIG  75 06 44 20 20  EA EA EA EA  D4
```

Decoded against the nRF905 RF_CONFIG register:

| Field | Value | Meaning |
| ---   | ---   | --- |
| `CH_NO` | `0x75` = **117** | RF channel |
| `HFREQ_PLL` | 1 (byte1 `0x06`) | 868/915 band → **f = (422.4 + 117/10) × 2 = 868.2 MHz** |
| `PA_PWR` | 1 | output power |
| `TX_AFW`/`RX_AFW` | 4 / 4 (byte2 `0x44`) | **4-byte address** |
| `RX_PW` / `TX_PW` | `0x20` / `0x20` | **32-byte payload** |
| `RX_ADDRESS` | `EA EA EA EA` | receive address |
| `CRC_EN` / `CRC_MODE` | 1 / 1 (byte9 `0xD4`) | **CRC enabled, 16-bit** |

A second command follows — `W_TX_ADDRESS` (`0x22`) + `EA EA EA EA` — so the
**TX address is also `EAEAEAEA`** (a point-to-point pair with the tank unit).

The computed **868.2 MHz matches the measured carrier** (~868.2–868.3 MHz), an
independent cross-check that this really is the radio firmware. This resolves the
rtl_433 unknowns `[U3]` (4-byte address `EAEAEAEA`), `[U4]` (**16-bit CRC**), and
`[U5]` (**32-byte payload**) — see [rtl433.md](rtl433.md).

## What the radio carries

The receive handler at `0x01c8` issues `R_RX_PAYLOAD` (`0x24`) and reads exactly
**`R3 = 0x20` = 32 bytes**, then runs a byte-by-byte frame parser. The transmit
side (`0x030b`) sends `W_TX_PAYLOAD` (`0x20`) and clocks out a **32-byte buffer in
internal RAM `0x30–0x4F`** (fetcher `0x052c`, index wraps at 31). So each
ShockBurst packet is a 32-byte payload; this control board **polls** (TX 32 B) and
reads the tank unit's **response** (RX 32 B) — the two-packet, ~62 s exchange seen
on air.

That radio buffer is filled from bytes the 8051 receives on its **serial
interrupt** (`0x029f`, standard 8051 `SCON`/`SBUF`; RX processor `0x03dc`), with a
matching outbound buffer at `0x50–0x6F` — i.e. the 8051 relays the live values it
gets from the MC9S08 over the inter-MCU link into the radio payload. The payload is
the slowly-changing telemetry captured in [rtl433.md](rtl433.md)
(`fd 7a ba ba ba 83` app header + data).

**Still open (`[U6]`):** which payload byte is water level / temperature / phase /
alarm. The container is fully known but the per-byte semantics are not — resolve by
tracing the RX processor `0x03dc` (what fills `0x30–0x4F`) against what the MC9S08
sends, or by a display-correlated capture. *No field map is asserted here yet — the
routines above are plumbing (buffers, SPI, a 32-bit mul/div library at `0x05b1`/
`0x061f`), not the field layout.*

Reproduce the disassembly with **[`tools/analyze-8051.sh`](../tools/analyze-8051.sh)
`Dump8051.java`** (headless Ghidra, 8051 processor).

## Optional: confirm the boot-serve on the bench

Not needed to get the image, but to *watch* U2 feed the radio: put a logic
analyzer on the SPI between U2 and the nRF9E5 (`SCK` / `MOSI` / `MISO` + the
nRF9E5 chip-select) and power-cycle. You will see the nRF9E5 master-read these
same 1648 bytes out of U2 at reset — the direct confirmation of the boot-serve
architecture above.
