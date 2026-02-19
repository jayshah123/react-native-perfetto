# Example app

This app validates `react-native-perfetto` against a real React Native runtime.

## What it demonstrates

- Session-based API (`startRecording` -> `TraceSession` -> `session.stop`).
- Manual `try/finally` section management with section handles.
- Helper-based lifecycle with `withSection` and `withRecording`.
- Counter and event args payloads.
- WebView bridge tracing via `createWebViewTraceBridge` and `react-native-webview`.
- Stable test IDs for Maestro automation.

## Quick Paths By Use Case

### RN-only tracing

Use these controls in the running sample app:

- `runWithRecordingDemoButton`
- `runOneSecondBusyLoopButton`

Recommended verification commands from repo root:

```sh
yarn maestro:test:capture-busy-loop-1s
yarn playwright:test:verify-busy-loop-1s
```

### RN + WebView tracing

Use this control in the running sample app:

- `runWebViewTracingDemoButton`

Recommended verification commands from repo root:

```sh
yarn maestro:test:capture-webview-trace
yarn playwright:test:verify-webview-trace
```

## Run

From repository root:

```sh
yarn
yarn example start
```

Then run a platform build:

```sh
yarn example android
# or
yarn example ios
```

If you vendor Perfetto SDK files under `cpp/third_party/perfetto/sdk/`, the app can produce full `.perfetto-trace` recordings.

## Maestro flow

Use the root-level Maestro flow to validate capture:

```sh
yarn maestro:install
yarn maestro:test:capture-trace
# or run the WebView bridge scenario:
yarn maestro:test:capture-webview-trace
```
