# TypeScript Public API

Source of truth: `src/index.tsx`

This is the primary supported API for app developers.

## Core Types

```ts
type TraceBackend = 'in-process' | 'system';
type TraceArg = string | number | boolean | null;
type TraceArgs = Record<string, TraceArg>;

interface RecordingOptions {
  filePath?: string;
  bufferSizeKb?: number;
  durationMs?: number;
  backend?: TraceBackend;
}

interface EventOptions {
  category?: string;
  args?: TraceArgs;
}

interface CounterOptions {
  category?: string;
  args?: TraceArgs;
}

interface StopResult {
  traceFilePath: string;
  bytesWritten?: number;
}

interface TraceSection {
  end(): void;
}

interface TraceSession {
  isActive(): boolean;
  stop(): Promise<StopResult>;
  section(name: string, options?: EventOptions): TraceSection;
  event(name: string, options?: EventOptions): void;
  counter(name: string, value: number, options?: CounterOptions): void;
}
```

## Functions

### `isPerfettoSdkAvailable(): boolean`

- Returns sync availability from native module.

### `startRecording(options?: RecordingOptions): Promise<TraceSession>`

- Starts a new active session.
- Rejects if another session is already active.
- Normalizes defaults:
  - `bufferSizeKb`: `4096`
  - `durationMs`: `0`
  - `backend`: `'in-process'`
  - `filePath`: empty string (native resolves default path)

### `withRecording(fn, options?): Promise<{ result, stop }>`

- Convenience helper:
  1. starts recording
  2. runs `fn(session)`
  3. always attempts `session.stop()`
- If task throws and stop also throws, stop error is dev-warned and task error is rethrown.

### `withSection(session, name, fn, options?): Promise<T>`

- Calls `session.section(...)`, runs `fn`, then always calls `section.end()` in `finally`.

### `createWebViewTraceBridge(options?)`

- Creates a dedicated bridge for `react-native-webview`.
- Returns:
  - `injectedJavaScriptBeforeContentLoaded`
  - `injectedJavaScript`
  - `onMessage(event)` relay handler
  - `getWebViewProps()`
  - `dispose()`
- In-page script exposes `window.ReactNativePerfetto` with:
  - `section`
  - `event`
  - `counter`
  - `withSection`

Options:

- `session?`: explicit target `TraceSession` (falls back to active default session if omitted).
- `sourceId?`: source identifier for this bridge.
- `defaultCategory?`: fallback category when page calls omit category.
- `mode?`: currently supports `'js-relay'`. `'native-direct'` throws `ERR_WEBVIEW_MODE_UNSUPPORTED`.
- `maxPayloadBytes?`: upper bound for single WebView message payload.

### `parseWebViewTracePayload(rawData, options)`

- Host-agnostic parser for WebView trace messages.
- Validates prefix, payload size, JSON, schema, protocol version, and optional source id.
- Returns:
  - `{ ok: true, operation, payload }`
  - `{ ok: false, reason, payload?, error? }`

Related exports:

- `WEBVIEW_TRACE_PROTOCOL_VERSION`
- `DEFAULT_WEBVIEW_TRACE_MAX_PAYLOAD_BYTES`
- `WebViewWireOperation`
- `ParseWebViewTracePayloadOptions`
- `ParseWebViewTracePayloadResult`
- `ParseWebViewTracePayloadFailureReason`

## `TraceSession` Methods

### `isActive()`

- `true` until stop finalization executes.

### `stop()`

- Idempotent per session after success (returns cached stop result).
- If called on inactive session without cached result, throws `ERR_NO_ACTIVE_SESSION`.

### `section(name, options?)`

- Starts a section immediately and returns `TraceSection`.
- Returns a no-op section when session is inactive (dev warning).
- Name fallback: `unnamed_section`.
- Category fallback: `react-native`.
- Args are normalized, filtered, key-sorted, and JSON serialized.

### `event(name, options?)`

- Emits instant event if session is active.
- Name fallback: `unnamed_event`.

### `counter(name, value, options?)`

- Emits counter if session is active and `value` is finite.
- Name fallback: `counter`.

## Error Codes

Main exported semantic error codes:

- `ERR_PERFETTO_UNAVAILABLE`
- `ERR_RECORDING_ALREADY_ACTIVE`
- `ERR_NO_ACTIVE_SESSION`
- `ERR_RECORDING_START_FAILED`
- `ERR_RECORDING_STOP_FAILED`
- `ERR_WEBVIEW_MODE_UNSUPPORTED`

Native-layer codes are normalized into the above where possible.

## Deprecated Compatibility Wrappers

These still route through session-first internals and emit dev warnings:

- `beginTraceSection(category, name)`
- `endTraceSection()`
- `instantTraceEvent(category, name, args?)`
- `setTraceCounter(name, value, options?)`
- `stopRecording()`
- `withTraceRecording(task, options?)`

Prefer `startRecording` + `TraceSession` + `withRecording`/`withSection`.
