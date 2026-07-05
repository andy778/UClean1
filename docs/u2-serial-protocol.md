# U2 serial / GSM-SMS controller (Path A)

The U2 CPU (Freescale MC9S08GT32) does **not** just drive the actuators — its
flash contains a full **GSM/SMS remote-monitoring controller**. It talks to a
Telit cellular modem over one of its two SCI ports and exposes a text command
interface that reports the plant's status, including the **cycle counter**
(*satsräknare*) shown on the display. That makes the serial/modem port the most
promising route to Home Assistant telemetry — it carries the counter and alarm
state in plain ASCII, no radio decoding required.

Everything below is recovered from `dumps/u2-mc9s08gt-flash.s19` and **now
confirmed on real hardware**: the fake-modem bench test reads live
`CYCLE COUNTER` / `PLANT STATUS` / `ALARM STATUS` and the full config out of U2
over the serial port (see "Validated on hardware" below).

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

## Validated on hardware ✅

The fake-modem test ([`tools/fake_telit.py`](../tools/fake_telit.py) `--sweep`)
was run against U2 on **board A** and **works end to end** — no SIM, no network.
U2 completes its full AT init against the fake modem, then answers an injected
command SMS with plain-ASCII telemetry.

**Command grammar (confirmed).** The SMS body is the unit's **4-character access
code**, optionally followed by a sub-command. `CODE` in the firmware help is a
*placeholder for that code*, **not** a literal keyword — bare `CODE` / `CODE CONF`
drew **no reply**; only the real code (`PASS`) did:

| SMS body      | reply |
| ---           | ---   |
| `PASS`        | plant status |
| `PASS CONF`   | config settings |
| `PASS CONF ?` | config help |
| `PASS RS`     | reset to defaults (destructive — not run) |

**Captured status (`PASS`), board A:**

```
CYCLE COUNTER:2129
PLANT STATUS:S102
ALARM STATUS:NO
```

**Captured config (`PASS CONF`), board A:**

```
CODE=PASS
REPLY=ON
HEARTBEAT=0
SMSCOUNTER=1
PHONE1=+358000000000
PHONE2=+358000000000
PHONE3=+358000000000
```

Notes:
- Two EEPROM facts gate the reply, both now verified: U2 answers only a **stored
  sender** (`PHONE1-3 = +358000000000`) and only when **`REPLY=ON`** (the `0x01`
  byte at EEPROM `0x3A98`). `HEARTBEAT=0` means U2 sends nothing on its own — you
  must poll it.
- **`CYCLE COUNTER:2129` is board A's own counter**, not board B's display
  *satsräknare* (1258); the two boards keep independent counts.
- **`PLANT STATUS:S102` = Aeration.** The `S`-codes are the treatment-cycle
  step, decoded from the Uponor Clean 1 manual (full table below).
- `ALARM STATUS` returns `NO` when clear; on a fault it carries the alarm text
  (`Compressor fault`, `MV1`-`MV5 fault`, `EEPROM error`, …).

**Home Assistant path.** Because `HEARTBEAT=0` and U2 replies only when polled,
an HA integration is a persistent fake-modem daemon that injects `PASS` on a
timer, parses the three status lines, and publishes them to MQTT (`cycle_counter`,
`plant_status`, `alarm_status`). No SIM, no cellular, no radio decode — the most
direct telemetry route found.

### `PLANT STATUS` `S<nnn>` codes (decoded from the manual)

The `S`-codes are the treatment-cycle step shown on the styrskåp display. The
hundreds digit is the cycle (1 = cleaning, 2 = waiting, 3 = maintenance,
4 = test), matching the firmware's cycle-prefixed phase table:

| Code(s)       | Meaning |
| ---           | --- |
| `S101`        | Filling the process tank |
| `S102`        | **Aeration** (the captured value) |
| `S103`–`S105` | Chemical dosing & mixing |
| `S106`–`S108` | Excess-sludge return + post-sedimentation (`S108` = 2nd sedimentation) |
| `S109`        | Discharge of cleaned water |
| `S201`–`S204` | Waiting mode (vänteläge) |
| `S301`–`S305` | Maintenance phase (underhållsfas) |
| `S400`        | Test cycle started |
| `S401`–`S408` | Test steps: pump-in, sludge-removal, pump-out, chem-fill, dose, settle, aeration I, aeration II |

### `ALARM STATUS` / fault `E`-codes (three views of one fault)

`ALARM STATUS` is `NO` when clear; a fault carries the alarm text, which the
manual also shows as a display `E`-code. These line up with the EEPROM alarm
table ([eeprom-map.md](eeprom-map.md)) — the SMS text, the display code, and the
firmware byte are the same fault:

| Display | SMS alarm text (EEPROM code) | Cause |
| ---     | ---                          | --- |
| `E011`  | No radio connection (`0x02`) | control-panel link lost |
| `E021`  | Chemical level low (`0x15`)  | low flocculant |
| `E031`  | High water; tank (`0x1F`)    | inlet module blocked |
| `E032`  | High water; outlet (`0x20`)  | outlet / pump-out blocked |
| `E040`  | Compressor fault (`0x28`)    | air-pump fault |
| `E041`  | MV1 fault (`0x29`)           | chemical-dosing valve |
| `E042`  | MV2 fault (`0x2A`)           | sludge-return valve |
| `E043`  | MV3 fault (`0x2B`)           | pump-out valve |
| `E044`  | MV4 fault (`0x2C`)           | pump-in valve |
| `E045`  | MV5 fault (`0x2D`)           | aeration valve |
| `E034`/`E047` | EEPROM error (`0x2F`)  | control-cabinet / program fault (exact pairing unconfirmed) |
| `E051`  | Sludge empty remind (`0x33`) | septic section filling |
| `E401`–`E403` | (service reminders)    | 1 / 3 / 6-year service due |
| `E000`  | — (normal / after reset)     | no fault |

So `MV1`–`MV5` are the five solenoid valves: dosing, sludge-return, pump-out,
pump-in, aeration.

Source: Uponor Clean I installation & operation manual (03/2026) — "Reningsverkets
status" (S-codes) and "Åtgärder vid störningar" (E-codes).

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

## Bench setup (how "Validated on hardware" above was done)

Cross-connect to the serial line — either an **RS-232 adapter on J5**, or
(simpler) a plain USB-UART on the **SP3232 TTL side** (adapter **RX ← U2 SCI2 TX**,
adapter **TX → U2 SCI2 RX**, common GND, **J5 5V pin left unconnected**) — 9600
8N1, and run [`tools/fake_telit.py`](../tools/fake_telit.py):

```
python3 tools/fake_telit.py --port /dev/ttyUSB0 --sweep
```

It answers U2's init handshake so U2 believes a registered modem is present,
then captures the SMS body U2 sends (`AT+CMGS`) — `CYCLE COUNTER:`,
`PLANT STATUS:`, `ALARM STATUS:`. `--sweep` auto-tries every candidate command;
without it, press Enter to inject `--inject` (default `PASS`) and exercise
`+CMTI` → `AT+CMGR` → `AT+CMGS`.

A full-GSM test (real SIM + antenna, text the unit its `PASS` query) is
possible but not needed — the bench tap already yields everything above.
