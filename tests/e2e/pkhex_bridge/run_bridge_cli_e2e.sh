#!/bin/zsh
set -euo pipefail

# Repo root = tests/e2e/pkhex_bridge/../../..
REPO_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BRIDGE_DLL=""
for cand in \
  "$REPO_ROOT/tools/pkhex_bridge/bin/Release/net10.0/PKHeXBridge.dll" \
  "$REPO_ROOT/tools/pkhex_bridge/bin/Debug/net10.0/PKHeXBridge.dll"; do
  if [[ -f "$cand" ]]; then
    BRIDGE_DLL="$cand"
    break
  fi
done

FIXTURE_DIR="$REPO_ROOT/tests/test-data/saves"
SAVE_PATH="$FIXTURE_DIR/Pokemonheartgold.sav"
if [[ ! -f "$SAVE_PATH" ]]; then
  SAVE_PATH="$FIXTURE_DIR/pokemon blue - ASH.sav"
fi
if [[ ! -f "$SAVE_PATH" ]]; then
  echo "No save fixture under $FIXTURE_DIR (expected HGSS or Blue copy)." >&2
  exit 1
fi
OUTPUT_FILE="$(mktemp /tmp/pkhex-bridge-e2e.XXXXXX)"

cleanup() {
  rm -f "$OUTPUT_FILE"
}
trap cleanup EXIT

if [[ -z "$BRIDGE_DLL" ]]; then
  echo "Bridge DLL not found under tools/pkhex_bridge/bin/(Release|Debug)/net10.0/" >&2
  exit 1
fi

DOTNET_CLI_HOME="$REPO_ROOT/../.dotnet" \
NUGET_PACKAGES="$REPO_ROOT/../.nuget/packages" \
dotnet "$BRIDGE_DLL" "$SAVE_PATH" >"$OUTPUT_FILE"

if ! grep -Fq '"success":true' "$OUTPUT_FILE"; then
  echo "Bridge E2E smoke test failed" >&2
  head -c 2000 "$OUTPUT_FILE" >&2
  echo >&2
  exit 1
fi

echo "[PASS] Bridge CLI emitted success JSON for committed fixture save"
