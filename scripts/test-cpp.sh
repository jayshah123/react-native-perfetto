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
