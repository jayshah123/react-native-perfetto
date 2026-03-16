# App Utilities JSI Example

This app demonstrates a C++ JSI module that exposes generic utility functions to JavaScript.

The public API is intentionally domain-focused (`math`, `logic`, `cache`).
Tracing is an internal implementation detail inside C++ and is not exposed through the JS surface.

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

## Notes

- The app installs bindings through a tiny native installer module.
- Utility host functions are implemented in `example-jsi/cpp/AppUtilitiesJSI.cpp`.
- Internal tracing uses `react_native_perfetto::Tracer` from the shared C++ wrapper.
