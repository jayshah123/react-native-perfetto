# C++ Module Public API

This document describes the current public C++ surface in `cpp/ReactNativePerfettoTracer.h` and its runtime behavior from `cpp/ReactNativePerfettoTracer.cpp`.

It is written in a modular format so each capability can be understood and consumed independently.

## Status and Compatibility

- Current direct C++ entry point: `react_native_perfetto::Tracer` (`cpp/ReactNativePerfettoTracer.h`).
- Intended audience today: this library's platform bridges and contributors.
- ABI stability today: not guaranteed for direct third-party binary integration.
- Planned stable native surface: C ABI (`extern "C"`) as documented in `docs/direct-cpp-api-exposure-plan.md`.

## Module Layout

The C++ API can be treated as four modules:

1. Capability and Session Control
2. Section Instrumentation
3. Instant Events
4. Counters

All modules are exposed via one singleton class: `react_native_perfetto::Tracer`.

## Header and Namespace

- Public header: `cpp/ReactNativePerfettoTracer.h`
- Namespace: `react_native_perfetto`
- Main type: `class Tracer`
- Configuration type: `struct RecordingConfig`

## Module 1: Capability and Session Control

### Type: `RecordingConfig`

```cpp
struct RecordingConfig {
  std::string file_path;
  uint32_t buffer_size_kb = 4 * 1024;
  uint32_t duration_ms = 0;
  bool enable_in_process_backend = true;
  bool enable_system_backend = false;
};
```

Field semantics:

- `file_path`: output path for the trace file. Empty means auto-generated temporary path.
- `buffer_size_kb`: Perfetto buffer size in KB. Default is `4096`.
- `duration_ms`: optional recording duration. `0` means no explicit duration limit.
- `enable_in_process_backend`: enables in-process backend.
- `enable_system_backend`: enables system backend.
- If both backend flags are false, implementation falls back to in-process backend.

### `static Tracer& Get()`

- Returns the process-wide singleton instance.

### `bool IsPerfettoSdkAvailable() const`

- `true` when Perfetto SDK headers/sources are available at build time.
- `false` when vendored SDK files are not present.

### `bool StartRecording(const RecordingConfig& config, std::string* error)`

Behavior:

- Fails if a recording is already active.
- Requires Perfetto SDK to be bundled.
- Initializes Perfetto SDK once (lazy init).
- Starts a new trace session and marks tracer state as active.

On failure, writes an error message to `error` when non-null.

### `bool StopRecording(std::string* output_path, std::string* error)`

Behavior:

- Fails if no recording is active.
- Stops current trace session.
- Reads bytes from Perfetto and writes to resolved output path.
- Clears active state and returns final path through `output_path` when non-null.

On failure, writes an error message to `error` when non-null.

## Module 2: Section Instrumentation

### `void BeginSection(const std::string& category, const std::string& name, const std::string& args_json)`

- Emits a begin marker for a scoped interval.
- Event name is composed as: `<category>:<name>|args=<args_json>` when args are present.
- Empty category falls back to `react-native`.
- Empty name falls back to `unnamed_event`.

### `void EndSection()`

- Ends the most recent section (LIFO behavior in C++ tracking).
- Safe to call even when there is no active Perfetto section id.

Usage expectation:

- Call in balanced pairs with `BeginSection()`.
- Prefer strict `try/finally` style in higher-level layers to guarantee balanced lifecycle.

## Module 3: Instant Events

### `void InstantEvent(const std::string& category, const std::string& name, const std::string& args_json)`

- Emits a point-in-time event with the same event name composition rules as sections.

## Module 4: Counters

### `void SetCounter(const std::string& category, const std::string& name, double value, const std::string& args_json)`

- Emits/updates a numeric counter event.
- Counter value is rounded to `int64_t` for Perfetto/ATrace emission.
- Uses same category/name/args naming rules as other events.

## Platform Behavior Matrix

### When Perfetto SDK is bundled

- `StartRecording` and `StopRecording`: fully supported.
- `BeginSection` / `EndSection`, `InstantEvent`, `SetCounter`: emitted to Perfetto track-event APIs.

### When Perfetto SDK is not bundled

- `StartRecording`: returns `false` with SDK-missing error.
- `StopRecording`: returns `false` with no-active/SDK-missing style error.
- Instrumentation methods still execute platform fallbacks where available.

## Platform Fallback Details

### Android

- Sections: `ATrace_beginSection` / `ATrace_endSection`
- Counters: `ATrace_setCounter` resolved dynamically via `dlsym` when available
- Build target is a shared library (`reactnativeperfetto` -> `libreactnativeperfetto.so`) via `android/CMakeLists.txt`

### iOS

- Sections/events use `os_signpost` APIs
- Section signpost ids are tracked per thread (`thread_local` stack)

## Error Strings (Current Implementation)

Current C++ errors produced by start/stop operations:

- `A Perfetto recording session is already running.`
- `No active recording session to stop.`
- `Perfetto SDK is not bundled. Add sdk/perfetto.h and sdk/perfetto.cc under cpp/third_party/perfetto/ to enable recording.`
- `Perfetto returned an empty trace payload.`
- `Failed to open output file: <resolved-path>`
- `Perfetto SDK is not available; nothing to stop.`

These are implementation details and may evolve. Higher layers map them to platform/JS error codes.

## Concurrency and Lifecycle Notes

- Internal mutable recording state is guarded by a mutex.
- Recording lifecycle is process-wide singleton state.
- Keep begin/end calls balanced and in consistent execution flow.
- For strongest cross-platform behavior, avoid crossing threads between begin/end pairs.

## Minimal Usage Example

```cpp
#include "ReactNativePerfettoTracer.h"

using react_native_perfetto::RecordingConfig;
using react_native_perfetto::Tracer;

void run_trace() {
  RecordingConfig config;
  config.enable_in_process_backend = true;
  config.enable_system_backend = false;

  std::string error;
  if (!Tracer::Get().StartRecording(config, &error)) {
    return;
  }

  Tracer::Get().BeginSection("app.native", "warm_start", "{}");
  // ... work ...
  Tracer::Get().EndSection();

  std::string output_path;
  Tracer::Get().StopRecording(&output_path, &error);
}
```

## Relationship to the Direct Native API Plan

- This C++ API is the current core API used by Android JNI and iOS ObjC++ bridges.
- For external host apps and dependent libraries, planned long-term stable integration is C ABI based.
- See `docs/direct-cpp-api-exposure-plan.md` for the packaging and compatibility target state.
