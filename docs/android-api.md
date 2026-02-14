# Android API (TurboModule + JNI)

Primary implementation files:

- `android/src/main/java/com/perfetto/PerfettoModule.kt`
- `android/src/main/jni/reactnativeperfetto-jni.cpp`

This layer adapts React Native TurboModule calls to the shared C++ tracer.

## TurboModule Methods

Exposed by `PerfettoModule`:

```kotlin
override fun isPerfettoSdkAvailable(): Boolean
override fun startRecording(
  filePath: String,
  bufferSizeKb: Double,
  durationMs: Double,
  backend: String,
  promise: Promise
)
override fun stopRecording(promise: Promise)
override fun beginSection(category: String, name: String, argsJson: String)
override fun endSection()
override fun instantEvent(category: String, name: String, argsJson: String)
override fun setCounter(
  category: String,
  name: String,
  value: Double,
  argsJson: String
)
```

## JNI Bridge Methods

Kotlin `external` methods map to JNI symbols:

```kotlin
private external fun nativeIsPerfettoSdkAvailable(): Boolean
private external fun nativeStartRecording(
  filePath: String,
  bufferSizeKb: Int,
  durationMs: Int,
  backend: String
): Boolean
private external fun nativeStopRecording(): String?
private external fun nativeBeginSection(category: String, name: String, argsJson: String)
private external fun nativeEndSection()
private external fun nativeInstantEvent(category: String, name: String, argsJson: String)
private external fun nativeSetCounter(category: String, name: String, value: Double, argsJson: String)
```

JNI then calls:

- `react_native_perfetto::Tracer::Get().IsPerfettoSdkAvailable()`
- `react_native_perfetto::Tracer::Get().StartRecording(...)`
- `react_native_perfetto::Tracer::Get().StopRecording(...)`
- `react_native_perfetto::Tracer::Get().BeginSection(...)`
- `react_native_perfetto::Tracer::Get().EndSection()`
- `react_native_perfetto::Tracer::Get().InstantEvent(...)`
- `react_native_perfetto::Tracer::Get().SetCounter(...)`

## Behavior Details

### Native library loading

- Android loads `reactnativeperfetto` via `System.loadLibrary("reactnativeperfetto")`.
- If loading fails:
  - availability returns `false`,
  - start/stop throw `IllegalStateException` and reject promise,
  - section/event/counter calls are no-op guarded by load check.

### Start recording

- If `filePath` is blank, defaults to:
  - `<cacheDir>/rn-perfetto-<timestamp>.perfetto-trace`
- `bufferSizeKb` and `durationMs` are cast to `Int`.
- Backend handling:
  - `"system"` => system backend enabled
  - anything else => in-process backend enabled
- Failures reject with code `ERR_PERFETTO_START`.

### Stop recording

- Resolves trace output path string on success.
- Rejects with `ERR_PERFETTO_STOP` when JNI throws or returns null path.

### Instrumentation

- `beginSection`, `endSection`, `instantEvent`, `setCounter` are synchronous bridge calls.

## C++ Error Propagation

JNI converts C++ start/stop failures into Java `IllegalStateException`.
Kotlin catches and rejects Promises:

- `ERR_PERFETTO_START`
- `ERR_PERFETTO_STOP`

TS layer later normalizes these into stable JS-facing error codes.

## Build Artifact Note

Android builds a shared native library in `android/CMakeLists.txt`:

- CMake target: `reactnativeperfetto`
- Output artifact: `libreactnativeperfetto.so`
