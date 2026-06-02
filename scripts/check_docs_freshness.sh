#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

required=(
  "docs/architecture/system-map.md"
  "docs/architecture/module-rules.md"
  "docs/agents/agent-playbook.md"
  "docs/agents/token-budget-policy.md"
  "docs/architecture/adrs/README.md"
  "docs/architecture/adrs/0000-template.md"
  "docs/engine/README.md"
  "docs/gameplay/README.md"
  "docs/transfer/README.md"
)

for path in "${required[@]}"; do
  if [[ ! -f "$path" ]]; then
    echo "[docs-check] missing required doc: $path" >&2
    exit 1
  fi
done

echo "[docs-check] OK"
