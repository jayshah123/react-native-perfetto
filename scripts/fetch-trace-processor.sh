#!/usr/bin/env bash
# Downloads the trace_processor wrapper into .tools/ (gitignored). The wrapper
# fetches and caches the pinned native trace_processor_shell binary on first
# use. Used by scripts/verify-trace-sql.py for SQL-level trace verification.
set -euo pipefail

cd "$(dirname "$0")/.."

mkdir -p .tools
curl -sL https://get.perfetto.dev/trace_processor -o .tools/trace_processor
chmod +x .tools/trace_processor
echo "[react-native-perfetto] trace_processor wrapper installed to .tools/"
