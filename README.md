# react-native-perfetto

Perfetto-first tracing for React Native apps with a typed JS/TS API, dual-architecture native bridges, and a shared C++ core.

## What this library provides

- Session-based tracing API for safe lifecycle management.
- `withRecording` and `withSection` helpers that enforce `try/finally` semantics.
- First-class manual section lifecycle via `beginSection(...)` / `endSection(...)`.
- Flat scalar args support for events/counters/sections.
- Dual-architecture bridge support for Android/iOS: TurboModule (New Architecture) and legacy NativeModule bridge (Old Architecture).
- Shared C++ tracer surface with a C-compatible wrapper for non-JS/native-host use cases.
- Compatibility wrappers for the previous global API.

## Big-Picture Use Cases

- Add low-overhead product instrumentation (startup, navigation, API latency, expensive compute) in React Native with trace files that can be inspected in Perfetto UI.
- Run repeatable end-to-end capture + verification in CI-style workflows using Maestro (capture) and Playwright SQL checks in Perfetto UI (verify expected slices and duration bounds).
- Trace across multiple runtimes by bridging WebView JavaScript events into the same RN session.
- Keep tracing fully internal for app-owned native utility modules while exposing only domain APIs to JavaScript (the `example-jsi` pattern).
- Reuse the native tracer from C/C++ entry points without routing through JS APIs when integrating in host-native layers.

## Key Design Decisions

- Session-first API (`startRecording` -> `TraceSession`) is the primary contract; wrappers are compatibility lanes, not the long-term architecture.
- `beginSection(...)` / `endSection(...)` are first-class and enforce strict LIFO semantics to match native tracing behavior and prevent JS/native stack drift.
- Shared C++ tracer logic is the source of truth, with Android/iOS bridges kept intentionally thin.
- App-owned utility JSI modules should keep tracing as an internal implementation detail, rather than exposing tracing primitives on app-level JS surfaces.
- Android release builds are intentionally non-debuggable in production-like configs, so `run-as`-based trace pulls are debug-only unless rooted-device strategies are used.

## Installation

```sh
npm install react-native-perfetto
```

or

```sh
yarn add react-native-perfetto
```

Then install iOS pods in your app:

```sh
cd ios
bundle exec pod install
```

## vNext API (recommended)

### Types

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

interface BeginSectionOptions extends EventOptions {
  session?: TraceSession;
}
```

### Entry points

- `isPerfettoSdkAvailable(): boolean`
- `getActiveSession(): TraceSession | null`
- `startRecording(options?): Promise<TraceSession>`
- `beginSection(name, options?)`
- `endSection(section)`
- `withRecording(fn, options?)`
- `withSection(session, name, fn, options?)`
- `createWebViewTraceBridge(options?)`

## Choose Your Integration Path

### Path A: React Native instrumentation only

Use this when all traced code runs in the React Native app runtime.

- Start here: section lifecycle APIs in this README under `Usage patterns` (examples 1-3).
- Primary API docs: `docs/ts-api.md`.
- Example app controls:
  - `runWithRecordingDemoButton`
  - `runOneSecondBusyLoopButton`
- Verification flow:
  - `yarn maestro:test:capture-busy-loop-1s`
  - `yarn playwright:test:verify-busy-loop-1s`

### Path B: React Native + WebView instrumentation

Use this when you need traces from `react-native-webview` JavaScript to feed the same trace session.

- Start here: `createWebViewTraceBridge(options?)` in this README (usage pattern 5).
- Full integration sample: `docs/webview-tracing-api.md`.
- Wire protocol details: `docs/webview-wire-protocol.md`.
- Example app control:
  - `runWebViewTracingDemoButton`
- Verification flow:
  - `yarn maestro:test:capture-webview-trace`
  - `yarn playwright:test:verify-webview-trace`

### Path C: Non-React-Native host + WebView protocol reuse

Use this when the host app is not RN but you still want compatible WebView message parsing.

- Reuse parser/types:
  - `parseWebViewTracePayload(...)`
  - `WebViewWireOperation`
- Docs:
  - `docs/webview-wire-protocol.md`
  - `docs/webview-tracing-api.md` (`Non-RN Reuse` section)

### Path D: App-Owned Utility JSI Module (Tracing Hidden Internally)

Use this when you want to keep tracing as an implementation detail inside native C++ while exposing a domain-focused utility API to JS.

- Example workspace app: `example-jsi`
- Run:
  - `yarn example:jsi start`
  - `yarn example:jsi android`
  - `yarn example:jsi ios`

### Path E: Native Host Integration Through C API

Use this when you need to call tracing from C/C++ host layers directly.

- Public C/C++ surface: `cpp/include/rnperfetto/tracer.h`
- C API implementation: `cpp/tracer_c_api.cpp`
- Keep this path focused on host-native integration boundaries; do not expand the public JS API solely to satisfy native-only use cases.

## Usage patterns

### 1) Manual `try/finally` section lifecycle

```ts
import { startRecording } from 'react-native-perfetto';

const session = await startRecording({ backend: 'in-process' });

const section = session.section('checkout-render', {
  category: 'checkout',
  args: { step: 'render', items: 3 },
});

try {
  // work
  session.event('checkout-render-done', {
    category: 'checkout',
    args: { status: 'ok' },
  });
  session.counter('checkout.items', 3, { category: 'checkout' });
} finally {
  section.end();
}

const stop = await session.stop();
console.log(stop.traceFilePath);
```

### 2) Helper-driven `try/finally`

```ts
import { startRecording, withSection } from 'react-native-perfetto';

const session = await startRecording();

try {
  await withSection(
    session,
    'api-fetch',
    async () => {
      // work
    },
    {
      category: 'network',
      args: { route: '/products' },
    }
  );
} finally {
  await session.stop();
}
```

### 3) Full helper lifecycle

```ts
import { withRecording, withSection } from 'react-native-perfetto';

const { result, stop } = await withRecording(async (session) => {
  return withSection(session, 'critical-path', async () => {
    return 'done';
  });
});

console.log(result, stop.traceFilePath);
```

### 4) Manual lifecycle from anywhere (active-session fallback)

```ts
import { beginSection, endSection, getActiveSession } from 'react-native-perfetto';

const activeSession = getActiveSession();
if (activeSession) {
  activeSession.event('checkout-started', { category: 'checkout' });
}

const section = beginSection('checkout-db-write', {
  category: 'checkout',
});

try {
  // work across callbacks / boundaries
} finally {
  endSection(section);
}
```

`beginSection(...)` prefers `options.session` and falls back to the active default session. Section ends are strict LIFO (matching native Perfetto/ATrace semantics), and out-of-order ends are ignored with a dev warning to preserve stack correctness.

### 5) WebView tracing (`react-native-webview`)

```tsx
import React, { useMemo } from 'react';
import { WebView } from 'react-native-webview';
import { createWebViewTraceBridge, startRecording } from 'react-native-perfetto';

const session = await startRecording();
const bridge = createWebViewTraceBridge({
  session,
  sourceId: 'checkout-webview',
  mode: 'js-relay',
});

<WebView source={{ uri: 'https://example.com' }} {...bridge.getWebViewProps()} />;
```

Inside page JS:

```js
window.ReactNativePerfetto.withSection('checkout-render', () => {
  window.ReactNativePerfetto.event('checkout-ready', {
    category: 'checkout',
    args: { step: 'render' },
  });
});
```

Host-agnostic protocol details and parser contract:

- `docs/webview-wire-protocol.md`

## Compatibility wrappers (deprecated)

The previous global API is still available as thin wrappers and emits a dev-only deprecation warning:

- `beginTraceSection(category, name)`
- `endTraceSection()`
- `instantTraceEvent(category, name, args?)`
- `setTraceCounter(name, value, options?)`
- `stopRecording()`
- `withTraceRecording(task, options?)`

Prefer `TraceSession` + `withSection`/`withRecording` or first-class manual `beginSection`/`endSection` for new code.

## Error codes

Promise failures use coded errors where possible:

- `ERR_PERFETTO_UNAVAILABLE`
- `ERR_RECORDING_ALREADY_ACTIVE`
- `ERR_NO_ACTIVE_SESSION`
- `ERR_RECORDING_START_FAILED`
- `ERR_RECORDING_STOP_FAILED`

## Enabling full Perfetto recording

Recording requires vendored Perfetto SDK files:

- `cpp/third_party/perfetto/sdk/perfetto.h`
- `cpp/third_party/perfetto/sdk/perfetto.cc`

Helper command:

```sh
yarn vendor:perfetto-sdk
```

Without SDK files, instrumentation APIs still work, but recording start/stop will fail with a descriptive error.

## Running the example app

```sh
yarn
yarn example start
```

Then:

```sh
yarn example android
# or
yarn example ios
```

## Running the legacy example app (RN 0.75, old architecture)

```sh
yarn
yarn example:legacy start
```

Then:

```sh
yarn example:legacy android
# or
yarn example:legacy ios
```

## Maestro E2E

The repository includes a basic Maestro flow that validates trace capture in the example app.
For a complete end-to-end process with diagrams (including release-build caveats), see `docs/testing-and-verification.md`.

Preconditions:

1. Perfetto SDK files are vendored (`yarn vendor:perfetto-sdk`).
2. An emulator/simulator is running.
3. The target app is installed and open on device:
   - `perfetto.example` (main example app)
   - `com.perfettolegacyexample` (legacy RN 0.75 app)

Important operational notes:

- `maestro:test:*` scripts in `package.json` are Android-default unless explicitly prefixed with `MAESTRO_PLATFORM=ios`.
- `example` and `example-jsi` both use `perfetto.example`, so reinstall the intended app before each flow when switching between them.
- `trace:pull:android` uses `run-as` and works for debuggable builds; for release verification use a rooted emulator/device and pull from `/data/user/0/<appId>/cache/rn-perfetto-*.perfetto-trace`.

Install Maestro CLI:

```sh
yarn maestro:install
```

Run capture flow:

```sh
yarn maestro:test:capture-trace
# legacy RN 0.75 app
yarn maestro:test:capture-trace:legacy
# or WebView bridge scenario
yarn maestro:test:capture-webview-trace
# or app-owned utility JSI scenario
yarn maestro:test:capture-app-utilities-trace
```

Flow files:

- `/.maestro/capture-trace.yaml`
- `/.maestro/capture-trace-legacy.yaml`
- `/.maestro/capture-webview-trace.yaml`
- `/.maestro/capture-app-utilities-trace.yaml`

Android Maestro scripts use `MAESTRO_PLATFORM=android` and auto-select a single connected ADB device to avoid interactive device prompts.

Run the dedicated 1s busy-loop capture flow:

```sh
yarn maestro:test:capture-busy-loop-1s
```

The flow file is `/.maestro/capture-busy-loop-1s.yaml`.

Pull the latest Android trace from app cache into `output/playwright/`:

```sh
yarn trace:pull:android
```

Pull the latest iOS simulator trace into `output/playwright/`:

```sh
yarn trace:pull:ios
```

Verify captured begin/end sections via Playwright + Perfetto SQL (defaults to
`withRecording-demo` and latest local trace in `output/playwright/`):

```sh
yarn playwright:test:verify-trace
```

Run verification with visible browser UI:

```sh
yarn playwright:test:verify-trace -- --headed
```

Keep the browser open after verification (for inspection):

```sh
yarn playwright:test:verify-trace -- --headed --keep-open
```

Run capture + pull + verification in one command (opens browser UI during verify):

```sh
yarn maestro:test:capture-and-verify-trace
```

Verify custom section substrings:

```sh
yarn playwright:test:verify-trace -- --event withRecording-demo --event manual-synthetic-work
```

Verify the app-utilities JSI flow event set:

```sh
yarn playwright:test:verify-app-utilities-trace
```

Verify the 1s busy-loop slice duration (expects `busy-loop-1s` max duration
between 900ms and 1500ms):

```sh
yarn playwright:test:verify-busy-loop-1s
```

Run 1s busy-loop capture + pull + duration verification in one command:

```sh
yarn maestro:test:capture-and-verify-busy-loop-1s
```

Run app-utilities capture + pull + verification in one command:

```sh
yarn maestro:test:capture-and-verify-app-utilities-trace
```

Run the iOS simulator variant:

```sh
yarn maestro:test:capture-and-verify-busy-loop-1s:ios
```

## Viewing traces

Open traces in Perfetto UI:

- <https://ui.perfetto.dev>

## Development

```sh
yarn test
yarn typecheck
yarn lint
yarn prepare
```

The unit test command runs:

- TypeScript protocol/parser tests under `tests/ts/` via Node's built-in test runner.
- Host C++ tracer tests under `cpp/tests/` via `scripts/test-cpp.sh`.

## API Documentation

- Cross-layer overview (TS -> Android -> C++): `docs/public-api-overview.md`
- TypeScript public API: `docs/ts-api.md`
- Android TurboModule + JNI API: `docs/android-api.md`
- C++ module public API: `docs/cpp-public-api.md`
- WebView tracing API: `docs/webview-tracing-api.md`
- WebView wire protocol: `docs/webview-wire-protocol.md`
- Direct native C API plan: `docs/direct-cpp-api-exposure-plan.md`

## References

- React Native Turbo Native Modules: <https://reactnative.dev/docs/turbo-native-modules-introduction>
- React Native C++ Turbo Modules: <https://reactnative.dev/docs/the-new-architecture/pure-cxx-modules>
- Perfetto tracing SDK: <https://perfetto.dev/docs/instrumentation/tracing-sdk>
- Perfetto track events: <https://perfetto.dev/docs/instrumentation/track-events>

## License

MIT
