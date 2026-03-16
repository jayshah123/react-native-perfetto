# App Utilities JSI Example

This app demonstrates a C++ JSI module that exposes generic utility functions to JavaScript.

The public API is intentionally domain-focused (`math`, `logic`, `cache`).
Tracing is an internal implementation detail inside C++ and is not exposed through the JS surface.

## Big-Picture Use Cases

- Validate the "app-owned utility module" integration style where tracing is used for observability but does not leak into product JS APIs.
- Demonstrate how to instrument C++ utility code with Perfetto sections/events while preserving a simple domain API contract for app developers.
- Provide a realistic E2E verification target for release builds via Maestro + Playwright Perfetto SQL checks.

## Architecture Snapshot

- JS surface: `example-jsi/src/appUtilities.ts` and `example-jsi/src/App.tsx`.
- Native installers:
  - Android: `example-jsi/android/app/src/main/java/perfetto/example/utilities/AppUtilitiesInstallerModule.kt` + JNI glue.
  - iOS: `example-jsi/ios/PerfettoExample/AppUtilitiesInstaller.mm`.
- C++ implementation: `example-jsi/cpp/AppUtilitiesJSI.cpp`.
- Shared tracer dependency: `react_native_perfetto::Tracer` from `cpp/ReactNativePerfettoTracer.*`.

## Design Decisions

- Tracing remains hidden in native internals; app JS receives utility results, not trace lifecycle controls.
- Utility operations emit stable trace names (for example, `app.runtime.utilities:utils.math.add`) so automated verification can assert behavior without coupling to UI text.
- Installer modules are intentionally minimal and only bridge runtime/bootstrap concerns.

## Utility API shown in this app

- `math.add(left, right)`
- `math.countPrimes(limit)`
- `logic.isPalindrome(value)`
- `cache.writeText(key, contents)`
- `cache.readText(key)`
- `cache.remove(key)`

## Run

From repository root:

```sh
yarn
yarn example:jsi start
```

Then run a platform build:

```sh
yarn example:jsi android
# or
yarn example:jsi ios
```

## E2E Trace Verification

From repository root:

```sh
# Android (default script path)
yarn maestro:test:capture-app-utilities-trace
yarn trace:pull:android
yarn playwright:test:verify-app-utilities-trace
```

```sh
# iOS simulator
MAESTRO_PLATFORM=ios ./scripts/run-maestro.sh test .maestro/capture-app-utilities-trace.yaml
yarn trace:pull:ios
yarn playwright:test:verify-app-utilities-trace
```

Notes:

- `example-jsi` and `example` share the `perfetto.example` package id; reinstall the intended app before each run when switching between flows.
- Android release trace pull through `yarn trace:pull:android` relies on `run-as` (debuggable builds). For rooted release verification, pull directly from `/data/user/0/perfetto.example/cache/`.

## Notes

- The app installs bindings through a tiny native installer module.
- Utility host functions are implemented in `example-jsi/cpp/AppUtilitiesJSI.cpp`.
- Internal tracing uses `react_native_perfetto::Tracer` from the shared C++ wrapper.
