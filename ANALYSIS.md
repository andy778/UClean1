# Analysis notes

Working notes for the reverse-engineering effort. The high-level plan has four
mostly-independent streams; each can be worked in its own branch/worktree.

| Stream | Goal | Status |
| ---    | ---  | ---    |
| Serial (Path A) | Tap the modem port for the cycle counter | **Working ✅** — U2 is a full GSM/SMS controller; impersonating the modem (no SIM) reads live `CYCLE COUNTER` / `PLANT STATUS` / `ALARM STATUS`. S/E-codes decoded from the manual. See [docs/u2-serial-protocol.md](docs/u2-serial-protocol.md). |
| Firmware (Path B) | Disassemble the U2 flash | **Done** — ~267 functions; I2C driver + SPI-slave finding mapped. See [docs/ghidra-firmware-analysis.md](docs/ghidra-firmware-analysis.md). |
| Radio PHY (Path C) | Recover the nRF905 config / on-air format | **Not in the U2 flash** (MC9S08 is SPI slave) — lives in the nRF9E5's 8051. Extraction plan written, bench not done. See [docs/nrf9e5-firmware.md](docs/nrf9e5-firmware.md). |
| EEPROM | Map the live records the firmware reads from U3 | **Done** — full 16 KB dumped and mapped (program table + GSM config; no live telemetry). See [docs/eeprom-map.md](docs/eeprom-map.md). |
| rtl_433 | FSK decoder for the 868.35 MHz link → MQTT → Home Assistant | **Frames recovered** — 100 kbps Manchester, `fd 7a ba ba ba 83` header. The counter is **not** on the radio (heartbeat is static across a counter tick); only batch *events* show. Byte-field mapping + trailer check still open. See [docs/rtl433.md](docs/rtl433.md). |

**Recommended next action:** Path A is the telemetry route. Build the
poll → parse → MQTT bridge (drive `tools/fake_telit.py`'s query loop, publish
`cycle_counter` / `plant_status` / `alarm_status`) for Home Assistant. The radio
streams carry batch events but not the numeric counter, so they are a secondary
signal at best.

## Reproducing the firmware analysis

```
tools/analyze.sh DumpReport.java                       # function map + SPI/IIC accesses
tools/analyze.sh DecompileCallers.java /tmp/out.txt a554 ded1   # follow a primitive outward
```

`analyze.sh` imports `dumps/u2-mc9s08gt-flash.s19` with `MotorolaHexLoader` and language
`HCS08:BE:16:MC9S08GB60`, in a throwaway project. Set `GHIDRA_HOME` if Ghidra is
not under `/opt/ghidra`.

Image layout: sparse — config bytes at `0x107F`/`0x1800`, code `0x8000-0xFFFF`,
vectors `0xFFD0-0xFFFF`, reset entry `0x807F`. ~267 functions recovered.

## Findings so far

### Drivers
- **SPI → nRF9E5 radio (slave side):** `0xBD56` is the SPI **receive interrupt
  handler** (vector `0xFFE0` = `BD 56`), not a config routine. Peripheral init
  writes `MOV #$CC,$28` → `SPI1C1=0xCC`, i.e. `MSTR=0`: the MC9S08 is the SPI
  **slave**. The nRF9E5's embedded 8051 is the SPI master and owns the nRF905
  `W_CONFIG` (channel/address/CRC). That config is therefore **not present in
  `dumps/u2-mc9s08gt-flash.s19`** — it must come from an SDR capture or a dump of the nRF9E5's
  internal program memory. The `SPI1D` accesses at `0xBD5B-0xBDB1` are the
  slave's byte handling, not config setup.
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
