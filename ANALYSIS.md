# Analysis notes

Working notes for the reverse-engineering effort. The high-level plan has four
mostly-independent streams; each can be worked in its own branch/worktree.

| Stream | Goal | Status |
| ---    | ---  | ---    |
| Serial (Path A) | Tap the modem port | **Dead** — no traffic at common baud rates |
| Radio / SPI | Extract the nRF905 config from firmware → native nRF905 receiver | In progress |
| EEPROM | Map the live records the firmware reads from U3 | Blocked on a full 16 KB dump |
| rtl_433 | FSK decoder for the 868.35 MHz link → MQTT → Home Assistant | Not started |

## Reproducing the firmware analysis

```
tools/analyze.sh DumpReport.java                       # function map + SPI/IIC accesses
tools/analyze.sh DecompileCallers.java /tmp/out.txt a554 ded1   # follow a primitive outward
```

`analyze.sh` imports `clean1.s19` with `MotorolaHexLoader` and language
`HCS08:BE:16:MC9S08GB60`, in a throwaway project. Set `GHIDRA_HOME` if Ghidra is
not under `/opt/ghidra`.

Image layout: sparse — config bytes at `0x107F`/`0x1800`, code `0x8000-0xFFFF`,
vectors `0xFFD0-0xFFFF`, reset entry `0x807F`. ~267 functions recovered.

## Findings so far

### Drivers
- **SPI → nRF9E5 radio:** byte-shift primitive ~`0xBD56`; `SPI1D` writes at
  `0xBD5B-0xBDB1`. The nRF905 `W_CONFIG` payload (channel/address/CRC) is set up
  here. Needs GUI stack-frame cleanup to read cleanly.
- **I2C → M24128 (U3):** bus driver `0xDED1-0xE03A`. `FUN_ded1` = start+send byte,
  `FUN_dfd2` = read byte, `FUN_df6a` = stop, `FUN_a554(buf, _, len, addrHi, addrLo)`
  = sequential read of `len` bytes from a 16-bit word address. Length-typed
  wrappers exist for 1/2/4/5/16/23/25-byte reads.
- **Peripheral init:** `0xFE80` (sets `SPI1C1/C2/BR`, `IIC1A/IIC1F`), runs from reset.

### EEPROM (U3) map
The firmware reads/writes records near the **top** of the chip — e.g. `0x3A9D`
(5-byte record), `0x3AD5` (2-byte), plus stride-8 indexed record arrays. The
README `i2cdump` only captured page 0 (`0x00-0xFF`) and used single-byte
addressing, so it **misses** the active region. Use `tools/dump_eeprom.sh` on the
Pi to get the full 16 KB (`0x3A00-0x3FFF` is the interesting part).
