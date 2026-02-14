#!/usr/bin/env bash
set -euo pipefail

APP_ID="${APP_ID:-perfetto.example}"
SIM_UDID="${SIM_UDID:-}"
OUTPUT_DIR="${1:-output/playwright}"

if [ -z "$SIM_UDID" ]; then
  SIM_UDID="$(xcrun simctl list devices booted | awk -F '[()]' '/Booted/ { print $2; exit }')"
fi

if [ -z "$SIM_UDID" ]; then
  echo "No booted iOS simulator found. Boot a simulator or set SIM_UDID." >&2
  exit 1
fi

data_dir="$(xcrun simctl get_app_container "$SIM_UDID" "$APP_ID" data 2>/dev/null || true)"
if [ -z "$data_dir" ] || [ ! -d "$data_dir" ]; then
  echo "Failed to locate app container for '$APP_ID' on simulator '$SIM_UDID'." >&2
  exit 1
fi

latest_trace_path="$(
  find "$data_dir" -type f -name 'rn-perfetto-*.perfetto-trace' -print0 \
    | xargs -0 ls -1t 2>/dev/null \
    | head -n 1
)"

if [ -z "$latest_trace_path" ]; then
  echo "No trace files were found for app id '$APP_ID' under simulator data container." >&2
  exit 1
fi

mkdir -p "$OUTPUT_DIR"

trace_file_name="$(basename "$latest_trace_path")"
local_trace_path="$OUTPUT_DIR/$trace_file_name"
cp "$latest_trace_path" "$local_trace_path"

echo "$local_trace_path"
