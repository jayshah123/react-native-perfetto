# Legacy Architecture Example (RN 0.75.4)

This app validates `react-native-perfetto` on an older React Native runtime with New Architecture disabled.

## Stack

- React Native: `0.75.4`
- React: `18.3.1`
- New Architecture / Fabric: disabled
  - Android: `example-legacy/android/gradle.properties` sets `newArchEnabled=false`
  - iOS: `example-legacy/ios/Podfile` sets `ENV['RCT_NEW_ARCH_ENABLED'] = '0'`

## What it demonstrates

- Session-first API (`startRecording` -> `TraceSession` -> `session.stop`)
- Manual and helper-based sections (`withSection`, `withRecording`)
- Event/counter args payloads
- WebView trace bridge demo (`react-native-webview`)

## Run

From repository root:

```sh
yarn
yarn example:legacy start
```

Then run a platform build:

```sh
yarn example:legacy android
# or
yarn example:legacy ios
```

## Maestro flow (Android)

From repository root, after the app is installed on the emulator:

```sh
yarn maestro:test:capture-trace:legacy
```

Flow file: `/.maestro/capture-trace-legacy.yaml`
