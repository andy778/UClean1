# U2 serial / GSM-SMS controller (Path A)

The U2 CPU (Freescale MC9S08GT32) does **not** just drive the actuators — its
flash contains a full **GSM/SMS remote-monitoring controller**. It talks to a
Telit cellular modem over one of its two SCI ports and exposes a text command
interface that reports the plant's status, including the **cycle counter**
(*satsräknare*) shown on the display. That makes the serial/modem port the most
promising route to Home Assistant telemetry — it carries the counter and alarm
state in plain ASCII, no radio decoding required.

Everything below is recovered from `dumps/u2-mc9s08gt-flash.s19`; the bench steps
are not yet done.

## Firmware evidence

Three independent findings in the dump identify U2 as a Telit modem host:

1. **Full Telit AT command set** (`strings` on the flash):
   `ATE0`, `AT+CMEE=1`, `AT+CPIN?`, `AT+CREG?`, `AT+CMGF=`, `AT+CSCS=`,
   `AT+CNMI=`, `AT+CPMS=`, `AT+CMGS=`, `AT+CMGR=`, `AT+CMGD=1,4`, `AT+IPR=`,
   `AT+IFC=0,0`, `AT#SELINT=`, `AT#BND=`, `AT+WMBS=`.
2. **GSM-specific reply parsing**: `+CPIN: READY`, `CME ERROR`, `CMS ERROR` —
   the firmware consumes modem result codes, so it is the DTE, not a passive port.
3. **SCI2 baud hard-coded to 9600**. At flash **`0xA40A`**:

   ```
   mov #$20,SCI2BDL      ; 4E 20 41  -> SBR = 0x0020 = 32
   ```

   Baud = f_bus / (16 × SBR) = f_bus / 512. A **4.9152 MHz** bus clock (a classic
   UART crystal) gives exactly **9600** — matching the `AT+IPR=9600` set on the
   modem. So **SCI2 is the modem UART at 9600 8N1**.

SCI1's baud divisor is loaded from the H:X register at runtime (no literal in the
dump), consistent with SCI1 being the **local RS-232 service port** for the
`CODE …` command interface below.

## The command / telemetry interface

The firmware implements a text protocol (delivered over SMS and/or the local
serial port). Recovered tokens:

- **Commands**: `CODE`, `CODE CONF`, `CODE CONF ?`, `CODE RS`.
- **Status fields** (what a query returns): `CYCLE COUNTER:`, `PLANT STATUS:S`,
  `ALARM STATUS:`, `SW Ver:`, `Proc Ver:`, `ID:`.
- **Config keys**: `CODE=`, `REPLY=`, `HEARTBEAT=`, `SMSCOUNTER=`,
  `PHONE1=` / `PHONE2=` / `PHONE3=`.
- **Alarm strings**: `No radio connection`, `Chemical level low`,
  `High water …`, `Compressor fault`, `MV1`–`MV5 fault`, `EEPROM error`,
  `Sludge empty remind`, `GSM fault`.

The stored placeholders are non-secret defaults (`PASS`, `+358000000000`).

`CYCLE COUNTER:` is exactly the display's *satsräknare* — reaching it over the
serial port is the goal of Path A.

## Hardware access — J5 "Modem" header

The board has a header **J5**, silk-labeled **"Modem"**, sitting next to U10
(SP3232) and U13. It is a **5 V TTL** modem-module header (multimeter mapped,
diode/continuity + powered voltage check), *not* an RS-232 port — so a plain
USB-UART reaches it directly, no MAX3232 needed. Pinout (top → bottom):

| J5 pin | label | notes |
| ---    | ---   | ---   |
| 1 | **5V**  | power **output** (reads ~5.0 V to GND with board powered); feeds the modem module — **do not** drive it from a USB-UART |
| 2 | *(unlabeled)* | 5th pin, function TBD (2nd GND / RING / power-key?) |
| 3 | **RX**  | serial |
| 4 | **TX**  | serial |
| 5 | **GND** | ground |

The board logic is **5 V** (the 5V pin reads ~5.0 V; a UART line idles high at the
logic rail). RX/TX are labeled by hand and their perspective (host vs. module) is
not yet pinned down — tier 2 below resolves it: the pin that *bursts* `AT…` at
power-up is U2's transmit.

This also places U10/SP3232 on a **separate** RS-232 service port, matching the
firmware's `CODE RS` command — i.e. **SCI2 → J5 (TTL) → modem**, while
**SCI1 → SP3232 → RS-232 service connector** carries the local `CODE …` interface.

> The earlier Path-A attempt ("port appeared dead, swept common bauds") is
> explained by this: with no modem attached U2 mostly stays quiet, and probing
> the wrong pin/direction (or the SP3232 RS-232 service port) sees nothing. Listen
> on **J5 TX** at power-up instead.

### Level / wiring cautions (5 V TTL)
- **Never connect a USB-UART's VCC to J5 pin 1.** It is a board output; only wire
  **GND + the two data lines**.
- If your adapter is 3.3 V: its 3.3 V TX into a 5 V input reads as a valid high,
  but protect its RX against the 5 V TX (series resistor / divider). A 5 V-capable
  adapter avoids the issue.
- Common ground between adapter and board.

## Verification plan

**Tier 1 — static (done).** The three firmware findings above.

**Tier 2 — passive scope (decisive, ~5 min).** Logic analyzer / USB-UART on
**J5 pins 3 and 4** (RX/TX), **9600 8N1**, GND to J5 pin 5, then power-cycle the
board. Prediction: a burst of printable ASCII — `ATE0`, `AT+CPIN?`,
`AT+CREG?`… The pin that bursts is U2's transmit (that also resolves the RX/TX
labels).

**Tier 3 — fake modem, no SIM (strongest bench proof).** Cross-connect a USB-UART
to J5 (adapter **RX ← J5 TX** = the bursting pin, adapter **TX → J5 RX**, GND to
J5 pin 5, **5V pin left unconnected**; mind the 5 V level cautions above) and run
[`tools/fake_telit.py`](../tools/fake_telit.py):

```
python3 tools/fake_telit.py --port /dev/ttyUSB0
```

It answers U2's init handshake so U2 believes a registered modem is present, then
**captures the SMS body U2 sends** (`AT+CMGS`) — which contains `CYCLE COUNTER:`,
`PLANT STATUS:` and `ALARM STATUS:`. Press Enter to inject a fake incoming
`CODE STATUS` SMS and exercise the query→reply path (`+CMTI` → `AT+CMGR`).

**Tier 4 — full GSM (end-to-end).** Real SIM + antenna; text the unit its
`CODE …` query and read the reply.

Recommended: **tier 2 → tier 3**. Tier 2 is a yes/no; tier 3 turns it into a
working, SIM-free telemetry tap that already yields the cycle counter.
