#!/bin/zsh
set -u

REPO_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
APP="$REPO_ROOT/build/title_screen_demo"
SMOKE_HOME="$(mktemp -d /tmp/pokemon-resort-smoke-home.XXXXXX)"
OUTPUT_FILE="$(mktemp /tmp/pokemon-resort-smoke-output.XXXXXX)"

cleanup() {
  rm -rf "$SMOKE_HOME"
  rm -f "$OUTPUT_FILE"
}
trap cleanup EXIT

if [[ ! -x "$APP" ]]; then
  echo "[FAIL] Native smoke could not find executable at $APP" >&2
  echo "Build it first with: cmake --build $REPO_ROOT/build --target title_screen_demo" >&2
  exit 1
fi

HOME="$SMOKE_HOME" \
SDL_VIDEODRIVER=dummy \
SDL_RENDER_DRIVER=software \
"$APP" >"$OUTPUT_FILE" 2>&1 &
APP_PID=$!

sleep 3
if kill -0 "$APP_PID" 2>/dev/null; then
  kill "$APP_PID" 2>/dev/null
  wait "$APP_PID" 2>/dev/null
  STATUS=124
else
  wait "$APP_PID"
  STATUS=$?
fi

OUTPUT="$(cat "$OUTPUT_FILE")"

if [[ "$STATUS" -ne 0 && "$STATUS" -ne 124 ]]; then
  echo "[FAIL] Native smoke exited unexpectedly with status $STATUS" >&2
  echo "$OUTPUT" >&2
  exit 1
fi

if [[ "$OUTPUT" == *"Fatal:"* || "$OUTPUT" == *"Segmentation fault"* || "$OUTPUT" == *"Assertion failed"* ]]; then
  echo "[FAIL] Native smoke saw a fatal startup message" >&2
  echo "$OUTPUT" >&2
  exit 1
fi

echo "[PASS] Native title screen booted headlessly for 3 seconds"
exit 0
