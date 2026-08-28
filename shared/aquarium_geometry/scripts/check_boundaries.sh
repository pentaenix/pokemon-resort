#!/usr/bin/env bash
set -euo pipefail

KERNEL_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPOSITORY_ROOT="$(cd "$KERNEL_ROOT/../.." && pwd)"

kernel_hits=$(rg -n '#include\s+[<"](SDL|bgfx|bx/|bimg/|imgui|core/|engine/|gameplay/|ui/|mapmaker/)' "$KERNEL_ROOT" -g'*.hpp' -g'*.cpp' || true)
if [[ -n "$kernel_hits" ]]; then
  echo "$kernel_hits"
  echo "[aquarium-kernel-boundary] forbidden runtime or tool dependency" >&2
  exit 1
fi

runtime_hits=$(rg -n 'AquariumGeometryJsonAdapter|aquarium_geometry_wasm' \
  "$REPOSITORY_ROOT/include" "$REPOSITORY_ROOT/src" "$REPOSITORY_ROOT/shared" \
  -g'*.hpp' -g'*.cpp' || true)
if [[ -n "$runtime_hits" ]]; then
  echo "$runtime_hits"
  echo "[aquarium-kernel-boundary] production code imports the tool-only WASM adapter" >&2
  exit 1
fi

echo "[aquarium-kernel-boundary] OK"
