#!/usr/bin/env bash
# Headless Ghidra analysis of the MC9S08GT (HCS08) flash dump.
# Imports clean1.s19 with the correct loader/processor, runs a script, throws
# the project away afterwards so runs are reproducible and stateless.
#
# Usage: tools/analyze.sh <ScriptName.java> [script args...]
#   GHIDRA_HOME overrides the Ghidra install location (default /opt/ghidra).
set -euo pipefail

GHIDRA="${GHIDRA_HOME:-/opt/ghidra}"
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT="${1:?usage: analyze.sh <ScriptName.java> [args...]}"
shift

if [[ ! -x "$GHIDRA/support/analyzeHeadless" ]]; then
  echo "analyzeHeadless not found under $GHIDRA (set GHIDRA_HOME)" >&2
  exit 1
fi

PROJDIR="$(mktemp -d)"
trap 'rm -rf "$PROJDIR"' EXIT

"$GHIDRA/support/analyzeHeadless" "$PROJDIR" proj \
  -import "$REPO/clean1.s19" \
  -loader MotorolaHexLoader \
  -processor "HCS08:BE:16:MC9S08GB60" \
  -scriptPath "$REPO/tools/ghidra" \
  -postScript "$SCRIPT" "$@" \
  -deleteProject
