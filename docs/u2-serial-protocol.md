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
(SP3232) and U13. It is a **2×5 (10-pin)** right-angle header. Tracing it out
with a multimeter (diode/continuity), **only two of its pins (2 & 3) run to
U10/SP3232**; the rest are mostly **GND**, plus (probably) a **5 V feed** for the
modem module — pin still to be confirmed (see below). Because those two data lines
come *through* the SP3232, **J5 is an RS-232 port (±V levels), not TTL** — the
SP3232 is the modem's line transceiver, not a separate service connector.

Pin numbering: **pin 1** is the corner marked by the silkscreen **white
triangle**, next to capacitor **C40**. Traced pins:

| J5 pin | goes to | SP3232 function | meaning |
| --- | --- | --- | --- |
| 1   | — | (triangle marker, by C40) | silkscreen ▶ reference corner |
| 2   | SP3232 **std pin 13** | **R1IN** (ch-1 RS-232 receiver input) | data **from modem** → board = board's **RX** |
| 3   | SP3232 **std pin 14** | **T1OUT** (ch-1 RS-232 driver output) | data **to modem** ← board = board's **TX** |
| 5   | — | — | **GND** |

> **SP3232 pin count is reversed from the datasheet.** The traces read as
> SP3232 "pin 4"/"pin 3" when counted from one end, but those are charge-pump
> capacitor pins — nonsense for a connector. Counted from the other end
> (std pin = 17 − n) they land on **std 13/14 = R1IN/T1OUT**, the channel-1
> RS-232 data pair, which is exactly right. So J5 pins 2 & 3 are the RS-232
> RX/TX going through SP3232 channel 1.

> **Open: the 5 V feed.** An earlier multimeter reading put ~5 V on pin 3, but
> pin 3 = T1OUT is a driver *output* that idles at the RS-232 **negative** rail —
> it can't be a 5 V supply. Re-measure pin 3 to GND (expect negative when idle,
> which also confirms it's the board TX); the actual modem 5 V feed is a
> different pin (7 or 9) still to be traced.

The two data pins (J5 2 & 3) are one SP3232 driver + receiver pair (RXD/TXD); the
firmware disables flow control (`AT+IFC=0,0`), so no handshake lines are wired
out — which fits "only two lines from the SP3232, rest GND." SP3232 is a **dual**
transceiver, so its second channel is the likely home of the local `CODE RS`
service port (SCI1).

Topology: **U2 SCI2 → U10/SP3232 → J5 (RS-232) → modem**. SCI2's 9600 8N1 (found
statically above) is the modem UART; the SP3232 shifts it to RS-232 for the Telit
module.

> The earlier Path-A attempt ("port appeared dead, swept common bauds") is
> consistent with this: with no modem attached U2 mostly stays quiet, and a plain
> USB-UART on the J5 data pins sees **inverted RS-232**, not TTL, so nothing
> decodes. Use an RS-232-level adapter on J5, or tap the SP3232 **TTL side**
> instead (below).

### Where to tap — J5 (RS-232) vs. SP3232 TTL side
A bare TTL USB-UART / logic analyzer will **not** decode the J5 data pins (they
are ±RS-232). Two options:

- **On J5**: use an RS-232-level adapter (a MAX3232/USB-RS232 cable), GND to a J5
  GND pin, **5 V pin left unconnected**.
- **On the SP3232 TTL side (recommended for bench work)**: probe the logic-level
  pins of U10 — the driver input (T_IN = U2 SCI2 TX) and receiver output
  (R_OUT = U2 SCI2 RX) — at **5 V TTL**, which a plain USB-UART / logic analyzer
  reads directly. Common ground to the board.

## Verification plan

**Tier 1 — static (done).** The three firmware findings above.

**Tier 2 — passive scope (decisive, ~5 min).** Probe at **9600 8N1**, then
power-cycle the board. Use a logic analyzer / plain USB-UART on the **SP3232 TTL
side** (U2 SCI2 TX = the driver input pin), or an **RS-232-level** adapter on the
J5 data pins. Prediction: a burst of printable ASCII — `ATE0`, `AT+CPIN?`,
`AT+CREG?`… The pin that bursts is U2's transmit.

**Tier 3 — fake modem, no SIM (strongest bench proof).** Cross-connect to the
serial line — either an **RS-232 adapter on J5**, or (simpler) a plain USB-UART on
the **SP3232 TTL side** (adapter **RX ← U2 SCI2 TX**, adapter **TX → U2 SCI2 RX**,
common GND, **J5 5V pin left unconnected**) — and run
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
