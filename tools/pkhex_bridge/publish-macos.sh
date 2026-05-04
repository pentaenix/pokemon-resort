#!/bin/zsh
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
OUTPUT_DIR="$SCRIPT_DIR/publish/osx-arm64"

DOTNET_CLI_HOME="${DOTNET_CLI_HOME:-$SCRIPT_DIR/../../.dotnet}"
NUGET_PACKAGES="${NUGET_PACKAGES:-$SCRIPT_DIR/../../.nuget/packages}"

mkdir -p "$OUTPUT_DIR"

DOTNET_CLI_HOME="$DOTNET_CLI_HOME" \
NUGET_PACKAGES="$NUGET_PACKAGES" \
dotnet publish "$SCRIPT_DIR/PKHeXBridge.csproj" \
  -c Release \
  -r osx-arm64 \
  --self-contained true \
  -o "$OUTPUT_DIR"

echo "Published bridge to $OUTPUT_DIR"
