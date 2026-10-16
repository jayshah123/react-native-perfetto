#!/usr/bin/env bash
set -euo pipefail

mkdir -p .test-dist/cpp

"${CXX:-c++}" \
  -std=c++20 \
  -Wall \
  -Wextra \
  -DRN_PERFETTO_WITH_SDK=0 \
  -Icpp \
  -Icpp/include \
  cpp/ReactNativePerfettoTracer.cpp \
  cpp/tracer_c_api.cpp \
  cpp/tests/ReactNativePerfettoTracer.test.cpp \
  -o .test-dist/cpp/reactnativeperfetto-tracer-test

./.test-dist/cpp/reactnativeperfetto-tracer-test

# When the vendored Perfetto SDK is present, also compile and run the
# SDK-enabled test binary which exercises real trace emission.
if [ -f cpp/third_party/perfetto/sdk/perfetto.cc ]; then
  "${CXX:-c++}" \
    -std=c++20 \
    -Wall \
    -Wextra \
    -Icpp \
    -Icpp/include \
    cpp/ReactNativePerfettoTracer.cpp \
    cpp/tracer_c_api.cpp \
    cpp/third_party/perfetto/sdk/perfetto.cc \
    cpp/tests/ReactNativePerfettoTracerSdk.test.cpp \
    -pthread \
    -o .test-dist/cpp/reactnativeperfetto-tracer-sdk-test

  ./.test-dist/cpp/reactnativeperfetto-tracer-sdk-test

  # When a trace_processor binary is available, additionally verify the
  # generated trace at the SQL level (slice names, categories, debug
  # annotations, counter tracks). Fetch it via scripts/fetch-trace-processor.sh.
  TP_BIN="${TRACE_PROCESSOR_BIN:-}"
  if [ -z "$TP_BIN" ] && [ -x .tools/trace_processor ]; then
    TP_BIN=".tools/trace_processor"
  fi
  if [ -z "$TP_BIN" ] && command -v trace_processor >/dev/null 2>&1; then
    TP_BIN="$(command -v trace_processor)"
  fi

  if [ -n "$TP_BIN" ]; then
    TRACE_PROCESSOR_BIN="$TP_BIN" python3 scripts/verify-trace-sql.py \
      /tmp/rn-perfetto-sdk-test.perfetto-trace \
      --event sdk-outer --arg attempt=3 --arg label=warmup \
      --category test.category
    TRACE_PROCESSOR_BIN="$TP_BIN" python3 scripts/verify-trace-sql.py \
      /tmp/rn-perfetto-sdk-test.perfetto-trace \
      --event sdk-event --arg phase=run --category test.category
    TRACE_PROCESSOR_BIN="$TP_BIN" python3 scripts/verify-trace-sql.py \
      /tmp/rn-perfetto-sdk-test.perfetto-trace \
      --event c-api-section --arg origin=c-abi --category test.category
    TRACE_PROCESSOR_BIN="$TP_BIN" python3 scripts/verify-trace-sql.py \
      /tmp/rn-perfetto-sdk-test.perfetto-trace \
      --counter sdk-counter=7.5
  else
    echo "[react-native-perfetto][cpp-test] trace_processor not found; skipping SQL-level verification"
  fi
else
  echo "[react-native-perfetto][cpp-test] vendored SDK not found; skipping SDK-enabled tests"
fi
