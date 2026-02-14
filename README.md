# react-native-perfetto

Perfetto-first tracing for React Native apps with a typed JS/TS API, TurboModule bridge, and shared C++ core.

## What this library provides

- Session-based tracing API for safe lifecycle management.
- `withRecording` and `withSection` helpers that enforce `try/finally` semantics.
- Flat scalar args support for events/counters/sections.
- TurboModule integration for Android and iOS with a shared C++ core.
- Compatibility wrappers for the previous global API.

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
```

### Entry points

- `isPerfettoSdkAvailable(): boolean`
- `startRecording(options?): Promise<TraceSession>`
- `withRecording(fn, options?)`
- `withSection(session, name, fn, options?)`

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

## Compatibility wrappers (deprecated)

The previous global API is still available as thin wrappers and emits a dev-only deprecation warning:

- `beginTraceSection(category, name)`
- `endTraceSection()`
- `instantTraceEvent(category, name, args?)`
- `setTraceCounter(name, value, options?)`
- `stopRecording()`
- `withTraceRecording(task, options?)`

Prefer `TraceSession` + `withSection`/`withRecording` for new code.

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

## Maestro E2E

The repository includes a basic Maestro flow that validates trace capture in the example app.
For a complete end-to-end process with diagrams (including release-build caveats), see `docs/testing-and-verification.md`.

Preconditions:

1. Perfetto SDK files are vendored (`yarn vendor:perfetto-sdk`).
2. An emulator/simulator is running.
3. The example app is installed and open on device (`perfetto.example`).

Install Maestro CLI:

```sh
yarn maestro:install
```

Run capture flow:

```sh
yarn maestro:test:capture-trace
```

The flow file is `/.maestro/capture-trace.yaml`.

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

Verify the 1s busy-loop slice duration (expects `busy-loop-1s` max duration
between 900ms and 1500ms):

```sh
yarn playwright:test:verify-busy-loop-1s
```

Run 1s busy-loop capture + pull + duration verification in one command:

```sh
yarn maestro:test:capture-and-verify-busy-loop-1s
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
yarn typecheck
yarn lint
yarn prepare
```

## References

- React Native Turbo Native Modules: <https://reactnative.dev/docs/turbo-native-modules-introduction>
- React Native C++ Turbo Modules: <https://reactnative.dev/docs/the-new-architecture/pure-cxx-modules>
- Perfetto tracing SDK: <https://perfetto.dev/docs/instrumentation/tracing-sdk>
- Perfetto track events: <https://perfetto.dev/docs/instrumentation/track-events>

## License

MIT
