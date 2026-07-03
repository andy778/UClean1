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

## Hardware access

The port is RS-232 via **U10 (SP3232)**, a 2-channel RS-232 transceiver. To reach
SCI2 you tap the **TTL side** at the SP3232 (the connector side is ±RS-232 and
will fry a 3.3 V UART):

| SP3232 pin | signal | direction |
| ---        | ---    | ---       |
| 11 `T1IN`  | TTL in  | from a U2 TxD |
| 10 `T2IN`  | TTL in  | from a U2 TxD |
| 12 `R1OUT` | TTL out | to a U2 RxD |
|  9 `R2OUT` | TTL out | to a U2 RxD |
| 14 `T1OUT` / 7 `T2OUT` | RS-232 out | to the connector |
| 13 `R1IN` / 8 `R2IN`   | RS-232 in  | from the connector |

Which SP3232 channel is SCI2 is not yet traced — identify it empirically in
tier 2 below (the one emitting 9600-baud `AT…` text at power-up).

> The earlier Path-A attempt ("port appeared dead, swept common bauds") is
> explained by this thesis: with no modem attached U2 mostly stays quiet, and a
> TTL adapter on the RS-232 connector (or on U2's RxD, where only the *modem*
> would talk) sees nothing. Listen on the **TxD/TTL** side instead.

## Verification plan

**Tier 1 — static (done).** The three firmware findings above.

**Tier 2 — passive scope (decisive, ~5 min).** Logic analyzer / 3.3 V USB-UART on
SP3232 `T1IN` (pin 11) and `T2IN` (pin 10), **9600 8N1**, then power-cycle the
board. Prediction: a burst of printable ASCII — `ATE0`, `AT+CPIN?`,
`AT+CREG?`… Seeing it on one pin confirms the thesis and identifies SCI2.

**Tier 3 — fake modem, no SIM (strongest bench proof).** Cross-connect a 3.3 V
USB-UART to that SCI2 pair (TX↔RX, common ground) and run
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
