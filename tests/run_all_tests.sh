#!/usr/bin/env bash
# Run the full repository test stack: .NET PKHeX bridge (unit, then integration), then native CTest
# (includes transfer/title harnesses, save_bridge_client_tests, resort tests, etc.).
#
# Bridge projects must not run in parallel (shared build output under tools/pkhex_bridge).
#
# Usage (from anywhere):
#   tests/run_all_tests.sh
#
# Optional bridge CLI e2e smoke (off by default; needs tools/pkhex_bridge built + saves under tests/test-data/saves/):
#   RUN_BRIDGE_CLI_E2E=1 tests/run_all_tests.sh
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

export DOTNET_CLI_HOME="${DOTNET_CLI_HOME:-$ROOT/../.dotnet}"
export NUGET_PACKAGES="${NUGET_PACKAGES:-$ROOT/../.nuget/packages}"

echo "== [1/3] PKHeX bridge unit tests =="
dotnet test "$ROOT/tests/unit/pkhex_bridge/PKHeXBridge.UnitTests/PKHeXBridge.UnitTests.csproj" \
  -c Release -v minimal

echo "== [2/3] PKHeX bridge integration tests =="
dotnet test "$ROOT/tests/integration/pkhex_bridge/PKHeXBridge.IntegrationTests/PKHeXBridge.IntegrationTests.csproj" \
  -c Release -v minimal

echo "== [3/3] Native CMake build + ctest =="
cmake -S "$ROOT" -B "$ROOT/build"
cmake --build "$ROOT/build"
ctest --test-dir "$ROOT/build" --output-on-failure

if [[ "${RUN_BRIDGE_CLI_E2E:-}" == "1" ]]; then
  echo "== [optional] Bridge CLI e2e smoke (RUN_BRIDGE_CLI_E2E=1) =="
  zsh "$ROOT/tests/e2e/pkhex_bridge/run_bridge_cli_e2e.sh"
fi

echo "All steps passed."
