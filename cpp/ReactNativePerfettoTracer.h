#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#if !defined(RN_PERFETTO_WITH_SDK)
#if __has_include("third_party/perfetto/sdk/perfetto.h")
#define RN_PERFETTO_WITH_SDK 1
#include "third_party/perfetto/sdk/perfetto.h"
#elif __has_include(<perfetto.h>)
#define RN_PERFETTO_WITH_SDK 1
#include <perfetto.h>
#else
#define RN_PERFETTO_WITH_SDK 0
#endif
#endif

namespace react_native_perfetto {

struct RecordingConfig {
  std::string file_path;
  uint32_t buffer_size_kb = 4 * 1024;
  uint32_t duration_ms = 0;
  bool enable_in_process_backend = true;
  bool enable_system_backend = false;
};

enum class OperationStatus {
  Ok,
  AlreadyRunning,
  NoActiveSession,
  Unsupported,
  Internal,
};

class Tracer {
 public:
  static Tracer &Get();

  bool IsPerfettoSdkAvailable() const;

  bool StartRecording(const RecordingConfig &config,
                      std::string *error,
                      OperationStatus *status = nullptr);
  bool StopRecording(std::string *output_path,
                     std::string *error,
                     OperationStatus *status = nullptr);

  void BeginSection(const std::string &category,
                    const std::string &name,
                    const std::string &args_json);
  void EndSection();
  void InstantEvent(const std::string &category,
                    const std::string &name,
                    const std::string &args_json);
  void SetCounter(const std::string &category,
                  const std::string &name,
                  double value,
                  const std::string &args_json);

  // C ABI helpers: these enforce same-thread LIFO semantics and only close
  // sections created through the C API surface.
  uint64_t BeginSectionFromCApi(const std::string &category,
                                const std::string &name,
                                const std::string &args_json);
  bool EndLastSectionFromCApi();
  bool EndSectionFromCApi(uint64_t handle);

 private:
  enum class SectionClosePolicy {
    Any,
    CApiOnly,
    CppOnly,
  };

  Tracer();

  Tracer(const Tracer &) = delete;
  Tracer &operator=(const Tracer &) = delete;

  std::string resolveOutputPath(const std::string &requested_path) const;
  std::string buildEventName(const std::string &category,
                             const std::string &name,
                             const std::string &args_json) const;
  void beginSectionImpl(const std::string &category,
                        const std::string &name,
                        const std::string &args_json,
                        bool c_api_owned,
                        uint64_t c_api_handle);
  bool endSectionImpl(SectionClosePolicy close_policy,
                      uint64_t required_c_api_handle);

#if RN_PERFETTO_WITH_SDK
  void ensurePerfettoInitialized(bool enable_system_backend);
#endif

  std::string current_output_path_;
  bool recording_ = false;

#if RN_PERFETTO_WITH_SDK
  std::once_flag perfetto_initialize_once_;
  std::unique_ptr<perfetto::TracingSession> tracing_session_;
#endif

#if defined(__APPLE__)
  void *signpost_log_ = nullptr;
#endif

  mutable std::mutex mutex_;
};

} // namespace react_native_perfetto
