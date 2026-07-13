# Register meanings — from the CPU datasheets, not guessed

This cross-references every peripheral register the two full decompiles
([`nrf9e5_full.c`](nrf9e5_full.c), [`mc9s08gt32_full.c`](mc9s08gt32_full.c))
actually touch, against the chips' own datasheets. Where Ghidra's default
processor module mislabels or fails to name an address, that is called out
explicitly.

## nRF9E5 (U1) — SFR addresses Ghidra can't name

Ghidra's generic `8051:BE:16:default` processor module knows the standard
8051 SFRs, but the nRF9E5 repurposes several SFR addresses for its own
radio/SPI/ADC/watchdog hardware (Nordic Semiconductor, *nRF9E5 Product
Specification v1.6* — mirror used to verify:
[keil.com PDF](https://www.keil.com/dd/docs/datashts/nordic/nrf9e5.pdf); the
README's archive.org link is the same document; §1.1.3 Table 3 is the SFR map,
§16.6 Table 45/46 covers the watchdog interface, §11.2 Table 21 covers the
nRF905 SPI instruction set):

| Addr | Ghidra's name | Real name | Function |
| --- | --- | --- | --- |
| `0x8E` | `CKCON` (generic 8052) | `CKCON` | Clock divider select for timers/UART — name happens to be correct |
| `0x91` | *(unnamed)* | `EXIF` | External interrupt flags. **Bit 5 (IE3) = SPI byte-transfer-done flag** (Table 39) |
| `0x94` | *(unnamed)* | `P0_DIR` | Port 0 direction |
| `0x95` | *(unnamed)* | `P0_ALT` | Port 0 alternate-function select |
| `0x96` | *(unnamed)* | `P1_DIR` | Port 1 direction (P1 low 4 bits are the boot-time SPI pins) |
| `0xAB` | *(unnamed)* | `REGX_MSB` | Indirect-register data, MSB (Table 45) |
| `0xAC` | *(unnamed)* | `REGX_LSB` | Indirect-register data, LSB |
| `0xAD` | *(unnamed)* | `REGX_CTRL` | bit4=busy, bit3=read(0)/write(1), bits2:0=index. **Index 0, write = `WWD`** (reload the 16-bit watchdog counter, Table 46) |
| `0xB2` | `IPL1` (generic 8052) | `SPI_DATA` | SPI shift register — **Ghidra's name is wrong** for this chip |
| `0xB3` | *(unnamed)* | `SPI_CTRL` | `00`=SPI unused, `01`=SPI routed to boot header (P1), `1x`=SPI routed to nRF905+ADC |
| `0xB4` | *(unnamed)* | `SPI_CLK` | SPI clock divider |

**P2 (SFR `0xA0`)** is the radio's GPIO-mapped control register (Table 15) —
read and write have *different* meanings per bit, because several nRF905
pins are shared:

| Bit | Write | Read |
| --- | --- | --- |
| 5 | `TRX_CE` (chip enable) | `DR` (data ready / TX complete) |
| 4 | `TX_EN` (1=TX, 0=RX) | `EOC` (ADC end of conversion) |
| 3 | `CSN` (SPI chip select, active low) | — |
| 6 | — | `CD` (carrier detect) |
| 2/1/0 | MOSI/SCK | MISO |

So in the decompile: `P2_3` is the nRF905 SPI chip select, `P2_5` is
`TRX_CE` when written and `DR`/"transfer complete" when read back, `P2_6`
(read) is carrier detect.

**nRF905 SPI opcodes** actually seen in the code, confirmed against the
official instruction set (Table 21) rather than inferred from byte patterns:
`0x00` = `W_RF_CONFIG` (write config, starting at byte 0), `0x20` =
`W_TX_PAYLOAD`, `0x22` = `W_TX_ADDRESS`, `0x24` = `R_RX_PAYLOAD`.

## MC9S08GT32ACFBE (U2) — mostly self-naming, one trap

Ghidra's `HCS08:BE:16:MC9S08GB60` processor module already names essentially
every peripheral register correctly for this chip — the GB60 and GT32 parts
share the same core and the same datasheet ("MC9S08GB/GT Data Sheet", NXP,
[MC9S08GB60.pdf](https://www.nxp.com/docs/en/data-sheet/MC9S08GB60.pdf)), so
no address renaming is needed. One name is misleading, though:

- **`SRS` is not a real register write.** SRS (System Reset Status, `$1800`)
  is read-only hardware. Per the datasheet (§5.8.2): *"Writing any value to
  this register address clears the COP watchdog timer without affecting the
  contents of this register."* Every `SRS = <value>;` line in
  [`mc9s08gt32_full.c`](mc9s08gt32_full.c) (44 occurrences) is the compiler's
  COP-watchdog-service idiom — almost always right after calling a
  state-machine module in the main loop (see `FUN_92fb`) — not a genuine
  store. Read it as "service the watchdog here," not as data.

Registers actually exercised and already interpreted in context elsewhere in
this repo: `SCI1C2`/`SCI2C2` (UART enable/TE/RE — modem + debug ports,
[docs/u2-serial-protocol.md](../u2-serial-protocol.md)), `SPI1C1`/`SPI1C2`
(SPI mode — nRF9E5 boot-image serving,
[docs/nrf9e5-firmware.md](../nrf9e5-firmware.md)), `IIC1*` (I2C — the M24128
EEPROM, [docs/eeprom-map.md](../eeprom-map.md)), `TPM1*` (timer/PWM — actuator
timing), `PTxD`/`PTxDD` (GPIO — output driver channels, see the README's
D6–D12/OC1–OC14 mapping).

## Open items (for the next pass)

- The MC9S08GT32ACFBE-specific datasheet variant (if NXP ever split it from
  the GB60 combined sheet) hasn't been checked for register differences —
  none found so far, but it's the one thing not independently verified here.
- `IIC1C1`'s exact bit table wasn't cross-checked against this datasheet copy
  (the GB60 sheet names it `IICC1`, without a module-number suffix — our chip
  variant's Ghidra pspec adds the `1`). Functionally unambiguous from
  context, just not re-verified bit-for-bit like `SRS`/`SCI1C2` were.
