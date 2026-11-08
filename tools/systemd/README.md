# Running the decoder + Home Assistant bridge as a service

Runs `rtl_433` (decoding the Uponor Clean 1 radio link) and
[`rtl_433_mqtt_hass_uclean1.py`](../rtl_433_mqtt_hass_uclean1.py) (the HA
discovery bridge) together as one `systemd --user` service, so both survive
logout and reboot without a terminal left open. See
[docs/rtl433-decoder.md](../../docs/rtl433-decoder.md) for what each piece
does and how to build `rtl_433` itself.

## Install

```bash
mkdir -p ~/.config/rtl433-uclean1
cp tools/systemd/run-uclean1-bridge.sh tools/systemd/credentials.env.example ~/.config/rtl433-uclean1/
mv ~/.config/rtl433-uclean1/credentials.env.example ~/.config/rtl433-uclean1/credentials.env
chmod 700 ~/.config/rtl433-uclean1/run-uclean1-bridge.sh
chmod 600 ~/.config/rtl433-uclean1/credentials.env
```

Edit `~/.config/rtl433-uclean1/credentials.env` with the real MQTT password
for the login you created on your broker (see "Home Assistant via MQTT
discovery" in docs/rtl433-decoder.md for creating one).

Edit `~/.config/rtl433-uclean1/run-uclean1-bridge.sh` and replace both
`YOUR_HA_HOST` placeholders with your Home Assistant / MQTT broker
hostname.

Requirements on `PATH` for the user the service runs as: `rtl_433` (built
per docs/rtl433-decoder.md) and `python3` with the
[`paho-mqtt`](https://pypi.org/project/paho-mqtt/) package installed
(`pip install --user paho-mqtt` or your distro's package).

```bash
mkdir -p ~/.config/systemd/user
cp tools/systemd/rtl433-uclean1.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now rtl433-uclean1.service
loginctl enable-linger "$USER"   # keep it running after you log out
```

## Check it's working

```bash
systemctl --user status rtl433-uclean1.service
journalctl --user -u rtl433-uclean1.service -f
```

New devices (`Uponor-Clean-1-<node id>`) should appear in Home Assistant
within about a minute (heartbeat cadence is ~62s).

## Notes

- The two processes are supervised together: if either `rtl_433` or the
  bridge exits, the wrapper script exits and systemd restarts the whole
  unit, relaunching both fresh. This avoids an orphaned `rtl_433` holding
  the RTL-SDR dongle (`usb_claim_interface` busy) across restarts.
- `-x 150` (in `run-uclean1-bridge.sh`) marks each entity `unavailable` in
  Home Assistant after ~2.5 missed heartbeat cycles, instead of leaving it
  silently frozen at its last value if a device stops transmitting.
- The script and service file are a starting point, not auto-updated —
  if you pull changes to `rtl_433_mqtt_hass_uclean1.py` or this directory,
  re-copy `run-uclean1-bridge.sh` and `rtl433-uclean1.service` and
  `systemctl --user daemon-reload && systemctl --user restart
  rtl433-uclean1.service`.
