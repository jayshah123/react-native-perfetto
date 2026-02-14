#!/usr/bin/env bash
set -euo pipefail

APP_ID="${APP_ID:-perfetto.example}"
OUTPUT_DIR="${1:-output/playwright}"

if ! command -v adb >/dev/null 2>&1; then
  echo "adb is not available in PATH." >&2
  exit 1
fi

mkdir -p "$OUTPUT_DIR"

latest_trace_path="$(
  adb shell run-as "$APP_ID" sh -c "ls -t cache/rn-perfetto-*.perfetto-trace 2>/dev/null | head -n 1" \
    | tr -d '\r'
)"

if [ -z "$latest_trace_path" ]; then
  echo "No trace files were found for app id '$APP_ID' in app cache." >&2
  exit 1
fi

trace_file_name="$(basename "$latest_trace_path")"
local_trace_path="$OUTPUT_DIR/$trace_file_name"

adb exec-out run-as "$APP_ID" cat "$latest_trace_path" > "$local_trace_path"

if [ ! -s "$local_trace_path" ]; then
  rm -f "$local_trace_path"
  echo "Pulled trace file is empty: $local_trace_path" >&2
  exit 1
fi

echo "$local_trace_path"
