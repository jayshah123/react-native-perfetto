#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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

class Tracer {
 public:
  static Tracer &Get();

  bool IsPerfettoSdkAvailable() const;

  bool StartRecording(const RecordingConfig &config, std::string *error);
  bool StopRecording(std::string *output_path, std::string *error);

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

 private:
  Tracer();

  Tracer(const Tracer &) = delete;
  Tracer &operator=(const Tracer &) = delete;

  std::string resolveOutputPath(const std::string &requested_path) const;
  std::string buildEventName(const std::string &category,
                             const std::string &name,
                             const std::string &args_json) const;

#if RN_PERFETTO_WITH_SDK
  void ensurePerfettoInitialized(bool enable_system_backend);
#endif

  std::string current_output_path_;
  bool recording_ = false;

#if RN_PERFETTO_WITH_SDK
  bool perfetto_initialized_ = false;
  std::unique_ptr<perfetto::TracingSession> tracing_session_;
  uint64_t next_section_track_id_ = 1;
  std::vector<uint64_t> active_section_track_ids_;
#endif

#if defined(__APPLE__)
  void *signpost_log_ = nullptr;
#endif

  mutable std::mutex mutex_;
};

} // namespace react_native_perfetto
