#!/usr/bin/env bash
# Dump the full 16 KB M24128 EEPROM (U3) over I2C, with correct 2-byte
# addressing. Run this ON THE RASPBERRY PI (or whatever hosts the I2C bus).
#
# i2cdump CANNOT do this: it only reaches offset 0x00-0xFF and sends a single
# address byte, but the M24128 needs a 14-bit (2-byte) address. The firmware's
# live records live near 0x3A00-0x3FFF, well outside i2cdump's reach.
#
# Usage: tools/dump_eeprom.sh [bus] [outfile]
#   bus     I2C bus number (default 1)
#   outfile output path (default dumps/u3-m24128-eeprom.bin)
#
# Prefer the kernel at24 driver when available (gives a clean binary); fall
# back to i2ctransfer chunked reads otherwise.
set -euo pipefail

BUS="${1:-1}"
OUT="${2:-dumps/u3-m24128-eeprom.bin}"
mkdir -p "$(dirname "$OUT")"
ADDR="0x50"
SIZE=16384          # 128 Kbit = 16 KB
CHUNK=128           # bytes per i2ctransfer read

sysfs="/sys/bus/i2c/devices/${BUS}-00$(printf '%02x' "$ADDR" 2>/dev/null || echo 50)/eeprom"

if command -v modprobe >/dev/null && [[ -e /sys/bus/i2c/devices/i2c-${BUS}/new_device ]]; then
  echo "Trying kernel at24 driver..." >&2
  sudo modprobe at24 || true
  if [[ ! -e "$sysfs" ]]; then
    echo "24c128 $ADDR" | sudo tee "/sys/bus/i2c/devices/i2c-${BUS}/new_device" >/dev/null || true
  fi
  if [[ -e "$sysfs" ]]; then
    sudo cp "$sysfs" "$OUT"
    echo "Wrote $OUT ($(wc -c <"$OUT") bytes) via at24" >&2
    exit 0
  fi
  echo "at24 path unavailable, falling back to i2ctransfer" >&2
fi

# Fallback: chunked random reads with 2-byte addressing.
: >"$OUT"
for (( off=0; off<SIZE; off+=CHUNK )); do
  hi=$(printf '0x%02x' $(( (off >> 8) & 0xff )))
  lo=$(printf '0x%02x' $(( off & 0xff )))
  i2ctransfer -y "$BUS" "w2@${ADDR}" "$hi" "$lo" "r${CHUNK}" \
    | tr ' ' '\n' | grep -E '^0x' \
    | while read -r b; do printf '%b' "\\x${b#0x}"; done >>"$OUT"
done
echo "Wrote $OUT ($(wc -c <"$OUT") bytes) via i2ctransfer" >&2
