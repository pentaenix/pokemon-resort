#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

fail() {
  echo "[boundary-check] $1" >&2
  exit 1
}

engine_hits=$(rg -n '#include\s+"(gameplay/|transfer/|resort/)' include/engine src/engine || true)
if [[ -n "$engine_hits" ]]; then
  echo "$engine_hits"
  fail "Engine domain has forbidden dependency on gameplay/transfer/resort"
fi

gameplay_hits=$(rg -n '#include\s+"(resort/|transfer/(?!contracts/))' include/gameplay src/gameplay -g'*.hpp' -g'*.cpp' -P || true)
if [[ -n "$gameplay_hits" ]]; then
  echo "$gameplay_hits"
  fail "Gameplay domain imports forbidden transfer internals or resort modules"
fi

transfer_contract_hits=$(rg -n '#include\s+"(gameplay/|ui/)' include/transfer/contracts || true)
if [[ -n "$transfer_contract_hits" ]]; then
  echo "$transfer_contract_hits"
  fail "Transfer contracts include gameplay/ui types"
fi

bash shared/aquarium_geometry/scripts/check_boundaries.sh

echo "[boundary-check] OK"
