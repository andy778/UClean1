#!/usr/bin/env bash
# Headless Ghidra analysis of the nRF9E5's embedded 8051 image.
#
# That image is not a separate chip — it lives inside the MC9S08 flash and is
# served to the nRF9E5 over SPI at boot. Extract it first (see
# docs/nrf9e5-firmware.md):
#   dd if=dumps/u2-mc9s08gt-flash.bin of=dumps/u1-nrf9e5-8051.bin \
#      bs=1 skip=$((0x703c)) count=$((0x670))
#
# This imports dumps/u1-nrf9e5-8051.bin as a raw binary at base 0x0000 with the
# 8051 processor, runs a script, and throws the project away so runs are
# reproducible and stateless.
#
# Usage: tools/analyze-8051.sh <ScriptName.java> [script args...]
#   GHIDRA_HOME overrides the Ghidra install location (default /opt/ghidra).
set -euo pipefail

GHIDRA="${GHIDRA_HOME:-/opt/ghidra}"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT="${1:?usage: analyze-8051.sh <ScriptName.java> [args...]}"
shift

IMG="$REPO/dumps/u1-nrf9e5-8051.bin"
if [[ ! -f "$IMG" ]]; then
  echo "missing $IMG — extract it from the U2 flash first (see docs/nrf9e5-firmware.md)" >&2
  exit 1
fi
if [[ ! -x "$GHIDRA/support/analyzeHeadless" ]]; then
  echo "analyzeHeadless not found under $GHIDRA (set GHIDRA_HOME)" >&2
  exit 1
fi

PROJDIR="$(mktemp -d)"
trap 'rm -rf "$PROJDIR"' EXIT

"$GHIDRA/support/analyzeHeadless" "$PROJDIR" proj \
  -import "$IMG" \
  -loader BinaryLoader \
  -processor "8051:BE:16:default" \
  -scriptPath "$REPO/tools/ghidra" \
  -postScript "$SCRIPT" "$@" \
  -deleteProject
