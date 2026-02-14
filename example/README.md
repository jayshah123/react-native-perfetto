# Example app

This app validates `react-native-perfetto` against a real React Native runtime.

## What it demonstrates

- Session-based API (`startRecording` -> `TraceSession` -> `session.stop`).
- Manual `try/finally` section management with section handles.
- Helper-based lifecycle with `withSection` and `withRecording`.
- Counter and event args payloads.
- Stable test IDs for Maestro automation.

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
```
