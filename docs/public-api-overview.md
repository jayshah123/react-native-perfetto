# Public API Overview (TS -> Android -> C++)

This repository exposes tracing APIs across four implementation layers:

1. TypeScript public API (`src/index.tsx`)
2. Android TurboModule + JNI bridge (`android/src/main/java/com/perfetto/PerfettoModule.kt`, `android/src/main/jni/reactnativeperfetto-jni.cpp`)
3. Shared C++ tracer core (`cpp/ReactNativePerfettoTracer.h`, `cpp/ReactNativePerfettoTracer.cpp`)
4. WebView bridge helper in TS (`createWebViewTraceBridge` in `src/index.tsx`)

This document maps those layers so contracts stay aligned.

## Layered Mapping

| Capability | TS entry point | Android bridge | C++ core |
| --- | --- | --- | --- |
| SDK availability | `isPerfettoSdkAvailable()` | `isPerfettoSdkAvailable()` -> `nativeIsPerfettoSdkAvailable()` | `Tracer::IsPerfettoSdkAvailable()` |
| Start recording | `startRecording(options)` | `startRecording(...)` -> `nativeStartRecording(...)` | `Tracer::StartRecording(...)` |
| Stop recording | `TraceSession.stop()` / `stopRecording()` (deprecated) | `stopRecording(...)` -> `nativeStopRecording()` | `Tracer::StopRecording(...)` |
| Begin section | `TraceSession.section(...)` / `beginTraceSection()` (deprecated) | `beginSection(...)` -> `nativeBeginSection(...)` | `Tracer::BeginSection(...)` |
| End section | `TraceSection.end()` / `endTraceSection()` (deprecated) | `endSection()` -> `nativeEndSection()` | `Tracer::EndSection()` |
| Instant event | `TraceSession.event(...)` / `instantTraceEvent()` (deprecated) | `instantEvent(...)` -> `nativeInstantEvent(...)` | `Tracer::InstantEvent(...)` |
| Counter | `TraceSession.counter(...)` / `setTraceCounter()` (deprecated) | `setCounter(...)` -> `nativeSetCounter(...)` | `Tracer::SetCounter(...)` |
| WebView tracing | `createWebViewTraceBridge(...)` | relays through `onMessage` and existing methods | reuses existing tracer methods through session APIs |

## Canonical Public Surface

- Product-facing API: TypeScript session-first API from `src/index.tsx`.
- Android bridge API: implementation contract for React Native codegen + JNI.
- C++ class API: shared core used by platform bridges; ABI is best-effort.
- WebView wire protocol parser: `src/webviewWireProtocol.ts` for host-agnostic message validation.

Planned stable native surface for external host apps/libraries is C ABI based:
- `docs/direct-cpp-api-exposure-plan.md`

## Lifecycle Contract

1. Start a recording session.
2. Emit section/event/counter instrumentation while session is active.
3. Stop session and collect output trace file path.

Constraints:

- Only one active session at TS layer (`startRecording` rejects if one is active).
- `TraceSection.end()` should be balanced with `session.section(...)`.
- Deprecated wrappers still work but route through the active session model.

## SDK Availability Contract

- With vendored Perfetto SDK files:
  - recording + instrumentation are available.
- Without vendored SDK files:
  - recording start/stop fail with explicit errors,
  - instrumentation still uses platform fallbacks where available.

See:
- `docs/ts-api.md`
- `docs/android-api.md`
- `docs/cpp-public-api.md`
- `docs/webview-tracing-api.md`
- `docs/webview-wire-protocol.md`
