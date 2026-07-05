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

## Optional: confirm the boot-serve on the bench

Not needed to get the image, but to *watch* U2 feed the radio: put a logic
analyzer on the SPI between U2 and the nRF9E5 (`SCK` / `MOSI` / `MISO` + the
nRF9E5 chip-select) and power-cycle. You will see the nRF9E5 master-read these
same 1648 bytes out of U2 at reset — the direct confirmation of the boot-serve
architecture above.
