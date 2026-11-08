#!/bin/bash
# Runs rtl_433 (decoding the Uponor Clean 1 radio link, publishing to MQTT)
# and the Home Assistant discovery bridge together as one systemd service.
# Each runs in a sub-shell job; if either exits, this script exits too
# (via `wait -n`), the trap kills the other, and systemd restarts the whole
# unit fresh -- this avoids an orphaned rtl_433 holding the RTL-SDR dongle
# (`usb_claim_interface` busy) on the next restart.
#
# MQTT_USERNAME/MQTT_PASSWORD come from credentials.env via the unit's
# EnvironmentFile=. Replace YOUR_HA_HOST below with your Home Assistant /
# MQTT broker host.
#
# -x 150 marks each HA entity unavailable after ~2.5 missed heartbeat
# cycles (heartbeat cadence is ~62s) instead of leaving it silently frozen
# at its last value forever.
set -euo pipefail

cleanup() {
    kill "${RTL_PID:-}" "${BRIDGE_PID:-}" 2>/dev/null || true
}
trap cleanup EXIT TERM INT

rtl_433 -f 868.05M -s 1000k -Y minmax \
    -F "mqtt://YOUR_HA_HOST:1883,user=${MQTT_USERNAME},pass=${MQTT_PASSWORD},retain=0,events=uclean1/events,devices=uclean1/events/devices[/model][/id]" &
RTL_PID=$!

python3 "$(dirname "$0")/../rtl_433_mqtt_hass_uclean1.py" \
    -H YOUR_HA_HOST -R uclean1/events -D homeassistant -x 150 &
BRIDGE_PID=$!

wait -n "$RTL_PID" "$BRIDGE_PID"
