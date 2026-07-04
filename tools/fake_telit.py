#!/usr/bin/env python3
"""Fake Telit GSM modem for bench-testing the Uponor Clean 1 U2 (MC9S08GT).

Thesis under test: U2 runs a GSM/SMS controller that drives a Telit modem over
SCI2 at 9600 8N1 (baud divisor SBR=32 found at flash 0xA40A -> 9600 @ 4.9152 MHz
bus). This script impersonates the modem so U2 completes its init handshake with
no SIM/network, and — the payoff — captures the status SMS U2 tries to SEND
(AT+CMGS), whose body contains CYCLE COUNTER / PLANT STATUS / ALARM STATUS.

Wiring: connect a 3.3 V USB-UART to the SCI2 lines (the SP3232 channel that emits
9600-baud AT text at power-up). Cross TX<->RX. Match logic levels: tap the TTL
side at SP3232 (T?IN / R?OUT), NOT the RS-232 connector, unless your adapter is
RS-232. Common ground.

SMS command grammar (from the firmware help strings, verbatim):
    <CODE>          -> "Status of the plant"  (CYCLE COUNTER: / PLANT STATUS: /
                       ALARM STATUS:) -- this is the satsräknare payoff
    <CODE> CONF     -> "Config settings"      (CODE:/REPLY:/HEARTBEAT:/PHONE1..3)
    <CODE> CONF ?   -> "Help for the config"
    <CODE> RS       -> "Back to the default settings"
where <CODE> is the unit's 4-character access code (firmware: "CODE: 4 char").
On board A that code is `PASS` (dumped from the U3 EEPROM at 0x3A9D). There is
NO "STATUS" verb — send the bare 4-char code to get plant status. A reply is only
sent if the unit's REPLY setting is ON.

Usage:
    python3 fake_telit.py --port /dev/ttyUSB0                     # Enter injects PASS
    python3 fake_telit.py --port /dev/ttyUSB0 --inject "PASS CONF"  # dump config

Press Enter at the console (after the AT handshake starts) to inject the --inject
text as a new incoming SMS, so U2 issues AT+CMGR, then AT+CMGS with its reply.
"""
import argparse
import sys
import threading
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial not installed:  pip install pyserial")

CRLF = b"\r\n"


def ts():
    return time.strftime("%H:%M:%S")


class FakeTelit:
    def __init__(self, ser, inject_text):
        self.ser = ser
        self.inject_text = inject_text
        self.have_injected_msg = False  # a pending "stored" incoming SMS at index 1
        self.seen_at = False            # U2 has completed at least one AT<->OK

    def send(self, text):
        """Send a Telit-style response line (framed with CRLF like a real modem)."""
        data = CRLF + text.encode() + CRLF if text else CRLF
        self.ser.write(data)
        print(f"[{ts()}] TX-> {text!r}")

    def ok(self):
        self.send("OK")

    def handle(self, cmd):
        print(f"[{ts()}] <-RX {cmd!r}")
        c = cmd.strip().upper()
        # U2 prefixes its AT lines with ESC (0x1b) to flush the modem to a known
        # state, and a noisy line-start can prepend stray bytes. .strip() does not
        # remove ESC, so resync to the first "AT" or the line is wrongly dropped as
        # non-AT noise and U2 never gets its OK (it then just retries AT forever).
        at = c.find("AT")
        if at > 0:
            c = c[at:]

        # --- SMS SEND: capture the telemetry body ---------------------------
        if c.startswith("AT+CMGS"):
            # Text mode: modem prompts with "> ", host sends body + Ctrl-Z(0x1A).
            self.ser.write(b"\r\n> ")
            print(f"[{ts()}] TX-> '> '  (waiting for SMS body + Ctrl-Z)")
            body = self.read_until_ctrlz()
            print("=" * 60)
            print(f"[{ts()}] *** CAPTURED OUTGOING SMS BODY (telemetry) ***")
            print(body.decode(errors="replace"))
            print("=" * 60)
            self.send("+CMGS: 1")
            self.ok()
            return

        # --- SMS READ: hand U2 the injected incoming command ----------------
        if c.startswith("AT+CMGR"):
            if self.have_injected_msg:
                # +CMGR: <stat>,<oa>,,<scts>  then the text  then OK
                self.send('+CMGR: "REC UNREAD","+358000000000",,"'
                          '00/01/01,00:00:00+00"')
                self.ser.write(self.inject_text.encode() + CRLF)
                print(f"[{ts()}] TX-> injected SMS text {self.inject_text!r}")
                self.have_injected_msg = False
                self.ok()
            else:
                self.send("+CMS ERROR: 321")  # invalid memory index
            return

        # --- SIM / registration / config: report happy, registered ----------
        if c.startswith("AT+CPIN"):
            self.send("+CPIN: READY"); self.ok(); return
        if c.startswith("AT+CREG"):
            self.send("+CREG: 0,1"); self.ok(); return   # registered, home
        if c.startswith("AT+CSQ"):
            self.send("+CSQ: 20,0"); self.ok(); return
        if c.startswith("AT+CPMS"):
            self.send("+CPMS: 1,50,1,50,1,50"); self.ok(); return
        if c.startswith("AT+CMGL"):
            # list messages: pretend none unless one injected
            if self.have_injected_msg:
                self.send('+CMGL: 1,"REC UNREAD","+358000000000",,')
                self.ser.write(self.inject_text.encode() + CRLF)
                self.have_injected_msg = False
            self.ok(); return

        # --- everything else (ATE0, CMEE, CMGF, CNMI, CSCS, IPR, IFC,
        #     #SELINT, #BND, WMBS, CMGD, plain AT ...): just acknowledge ------
        if c.startswith("AT"):
            if not self.seen_at:
                self.seen_at = True
                print(f"[{ts()}] handshake started — U2 is talking; you can inject now")
            self.ok(); return

        # non-AT noise
        print(f"[{ts()}] (ignored non-AT input {cmd!r})")

    def read_until_ctrlz(self, timeout=5.0):
        buf = bytearray()
        end = time.time() + timeout
        while time.time() < end:
            b = self.ser.read(1)
            if not b:
                continue
            if b[0] == 0x1A:      # Ctrl-Z = send
                break
            if b[0] == 0x1B:      # ESC = abort
                break
            buf += b
        return bytes(buf)

    def reader_loop(self):
        line = bytearray()
        while True:
            b = self.ser.read(1)
            if not b:
                continue
            if b[0] in (0x0D, 0x0A):   # CR or LF ends a command
                if line:
                    self.handle(bytes(line).decode(errors="replace"))
                    line = bytearray()
            else:
                line += b

    def console_loop(self):
        for _ in sys.stdin:
            if not self.seen_at:
                print(f"[{ts()}] not injecting yet — U2 hasn't completed its AT "
                      "handshake (no ATE0/CPIN traffic seen). Wait for it, then "
                      "press Enter.")
                continue
            self.have_injected_msg = True
            # Nudge U2 to read it: unsolicited new-message indication.
            self.send('+CMTI: "SM",1')
            print(f"[{ts()}] queued injected SMS {self.inject_text!r}; sent +CMTI")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True, help="e.g. /dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=9600)
    ap.add_argument("--inject", default="PASS",
                    help="incoming-SMS text sent on Enter. Default 'PASS' = board "
                         "A's 4-char access code -> plant status. Try 'PASS CONF' "
                         "for config. (No 'STATUS' verb exists; send the bare code.)")
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, bytesize=8, parity="N",
                        stopbits=1, timeout=0.2)
    print(f"[{ts()}] fake Telit on {args.port} @ {args.baud} 8N1")
    print("Press Enter to inject an incoming command SMS. Ctrl-C to quit.")
    ft = FakeTelit(ser, args.inject)
    threading.Thread(target=ft.reader_loop, daemon=True).start()
    try:
        ft.console_loop()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
