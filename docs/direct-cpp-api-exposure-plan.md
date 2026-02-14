# Direct C++ API Exposure Plan (with Examples)

## Summary
Your C++ core is implemented and reusable, but not yet packaged as a supported direct public API for host apps or other RN libraries.  
The plan is to expose a stable **C ABI** across iOS/Android, keep current C++ API as convenience, and publish native headers/artifacts so app/lib consumers can link directly.

## Key decisions locked
1. Audience: host apps + other RN libraries.
2. Stable contract: C ABI (`extern "C"`), C++ API best-effort.
3. Release: minor version.

## Required implementation changes

### 1) Add stable C API surface
1. Create `cpp/public/react_native_perfetto_c.h`.
2. Create `cpp/public/react_native_perfetto_c.cpp`.
3. Export `rnp_*` functions:
- `rnp_is_sdk_available`
- `rnp_start_recording`
- `rnp_stop_recording`
- `rnp_begin_section`
- `rnp_end_section`
- `rnp_instant_event`
- `rnp_set_counter`

### 2) iOS packaging
1. Update `Perfetto.podspec`:
- `public_header_files` => `cpp/public/**/*.h`
- internal headers remain private.
2. Add `header_mappings_dir` so imports are clean and stable.
3. Keep module support (`DEFINES_MODULE = YES`).

### 3) Android packaging
1. In `android/build.gradle` enable:
- `buildFeatures { prefab true; prefabPublishing true }`
2. Add header-copy task for public headers only.
3. Add `prefab { reactnativeperfetto { headers ".../headers/reactnativeperfetto/" } }`.

### 4) Bridge alignment
1. JNI and iOS bridge should call C ABI internally.
2. Keep all layer semantics/errors aligned with current TS API.

### 5) Docs
1. Add “Direct Native API” section in `README.md`.
2. Add integration examples for host app and dependent RN library.
3. Add compatibility policy: C ABI stable, C++ API best-effort.

## Examples to include

### Example A: C API header shape
```c
// react_native_perfetto_c.h
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char* file_path;
  uint32_t buffer_size_kb;
  uint32_t duration_ms;
  int enable_in_process_backend;
  int enable_system_backend;
} rnp_recording_config_t;

int rnp_start_recording(const rnp_recording_config_t* config, char* err, size_t err_len);
int rnp_stop_recording(char* out_path, size_t out_len, char* err, size_t err_len);
void rnp_begin_section(const char* category, const char* name, const char* args_json);
void rnp_end_section(void);

#ifdef __cplusplus
}
#endif
```

### Example B: iOS consumer (ObjC++)
```objc
#import <Perfetto/react_native_perfetto_c.h>

rnp_begin_section("app.native", "warm_start", "{}");
// work...
rnp_end_section();
```

### Example C: Android consumer CMake
```cmake
find_package(reactnativeperfetto REQUIRED CONFIG)

target_link_libraries(my_native_lib
  reactnativeperfetto::reactnativeperfetto
)
```

### Example D: Android consumer C++
```cpp
#include <react_native_perfetto/react_native_perfetto_c.h>

rnp_begin_section("app.native", "db_init", "{}");
// work...
rnp_end_section();
```

## Tests and acceptance criteria
1. Existing TS flows remain unchanged and passing.
2. iOS sample imports public header and emits trace section.
3. Android sample links via Prefab and emits trace section.
4. SDK-missing behavior returns deterministic error via C ABI.
5. Build artifacts include exported symbols and public headers.

## Assumptions/defaults
1. No breaking changes to TS/Java/ObjC APIs.
2. C ABI is official compatibility target.
3. C++ class API remains available but non-stable ABI.
