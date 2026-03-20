#include "ReactNativePerfettoTracer.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

#if defined(__ANDROID__)
#include <android/trace.h>
#include <dlfcn.h>
#endif

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IOS
#include <os/log.h>
#include <os/signpost.h>
#endif
#endif

#if RN_PERFETTO_WITH_SDK
PERFETTO_DEFINE_CATEGORIES(
    ::perfetto::Category("react-native"),
    ::perfetto::Category("react-native.native"),
    ::perfetto::Category("react-native.js"),
    ::perfetto::Category("react-native.perfetto"));

PERFETTO_TRACK_EVENT_STATIC_STORAGE();
#endif

namespace react_native_perfetto {

namespace {

std::string buildDefaultPath() {
  std::ostringstream builder;
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

#if defined(__ANDROID__)
  const auto base_path = std::filesystem::temp_directory_path();
#elif defined(__APPLE__)
  const auto base_path = std::filesystem::temp_directory_path();
#else
  const auto base_path = std::filesystem::current_path();
#endif

  builder << (base_path / ("rn-perfetto-" + std::to_string(millis) +
                           ".perfetto-trace"))
                 .string();

  return builder.str();
}

#if defined(__ANDROID__)
using ATraceSetCounterFn = void (*)(const char *, int64_t);

ATraceSetCounterFn resolveSetCounterFn() {
  static ATraceSetCounterFn fn =
      reinterpret_cast<ATraceSetCounterFn>(dlsym(RTLD_DEFAULT,
                                                 "ATrace_setCounter"));
  return fn;
}
#endif

std::string sanitizeEventName(const std::string &value,
                              const std::string &fallback) {
  if (value.empty()) {
    return fallback;
  }

  return value;
}

struct SectionFrame {
  bool c_api_owned = false;
  uint64_t c_api_handle = 0;

#if RN_PERFETTO_WITH_SDK
  uint64_t sdk_track_id = 0;
#endif

#if defined(__APPLE__) && TARGET_OS_IOS
  os_signpost_id_t signpost_id = OS_SIGNPOST_ID_NULL;
#endif
};

std::atomic<uint64_t> g_next_c_api_section_handle{1};
thread_local std::vector<SectionFrame> g_section_stack;

#if RN_PERFETTO_WITH_SDK
std::atomic<uint64_t> g_next_sdk_section_track_id{1};
#endif

uint64_t nextNonZeroHandle(std::atomic<uint64_t> &counter) {
  uint64_t next = counter.fetch_add(1, std::memory_order_relaxed);
  if (next == 0) {
    next = counter.fetch_add(1, std::memory_order_relaxed);
  }
  return next;
}

} // namespace

Tracer &Tracer::Get() {
  static Tracer instance;
  return instance;
}

Tracer::Tracer() {
#if defined(__APPLE__) && TARGET_OS_IOS
  signpost_log_ = os_log_create("com.reactnativeperfetto", "trace");
#endif
}

bool Tracer::IsPerfettoSdkAvailable() const {
#if RN_PERFETTO_WITH_SDK
  return true;
#else
  return false;
#endif
}

std::string Tracer::resolveOutputPath(const std::string &requested_path) const {
  if (!requested_path.empty()) {
    return requested_path;
  }

  return buildDefaultPath();
}

std::string Tracer::buildEventName(const std::string &category,
                                   const std::string &name,
                                   const std::string &args_json) const {
  const auto resolved_name = sanitizeEventName(name, "unnamed_event");
  const auto resolved_category = sanitizeEventName(category, "react-native");

  std::string result = resolved_category + ":" + resolved_name;
  if (!args_json.empty()) {
    result += "|args=" + args_json;
  }

  return result;
}

bool Tracer::StartRecording(const RecordingConfig &config,
                            std::string *error,
                            OperationStatus *status) {
  if (status != nullptr) {
    *status = OperationStatus::Ok;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  if (recording_) {
    if (status != nullptr) {
      *status = OperationStatus::AlreadyRunning;
    }
    if (error != nullptr) {
      *error = "A Perfetto recording session is already running.";
    }
    return false;
  }

#if RN_PERFETTO_WITH_SDK
  ensurePerfettoInitialized(config.enable_system_backend);

  perfetto::TraceConfig trace_config;
  trace_config.add_buffers()->set_size_kb(config.buffer_size_kb);

  auto *data_source_config = trace_config.add_data_sources()->mutable_config();
  data_source_config->set_name("track_event");

  perfetto::protos::gen::TrackEventConfig track_event_config;
  track_event_config.add_enabled_categories("react-native");
  track_event_config.add_enabled_categories("react-native.native");
  track_event_config.add_enabled_categories("react-native.js");
  data_source_config->set_track_event_config_raw(
      track_event_config.SerializeAsString());

  if (config.duration_ms > 0) {
    trace_config.set_duration_ms(config.duration_ms);
  }

  uint32_t backend_mask = 0;
  if (config.enable_in_process_backend) {
    backend_mask |= static_cast<uint32_t>(perfetto::kInProcessBackend);
  }
  if (config.enable_system_backend) {
    backend_mask |= static_cast<uint32_t>(perfetto::kSystemBackend);
  }
  if (backend_mask == 0) {
    backend_mask = static_cast<uint32_t>(perfetto::kInProcessBackend);
  }

  const auto backend_type =
      static_cast<perfetto::BackendType>(backend_mask);
  tracing_session_ = perfetto::Tracing::NewTrace(backend_type);
  tracing_session_->Setup(trace_config);
  tracing_session_->StartBlocking();

  current_output_path_ = resolveOutputPath(config.file_path);
  recording_ = true;
  return true;
#else
  (void)config;
  if (status != nullptr) {
    *status = OperationStatus::Unsupported;
  }
  if (error != nullptr) {
    *error =
        "Perfetto SDK is not bundled. Add sdk/perfetto.h and sdk/perfetto.cc "
        "under cpp/third_party/perfetto/ to enable recording.";
  }
  return false;
#endif
}

bool Tracer::StopRecording(std::string *output_path,
                           std::string *error,
                           OperationStatus *status) {
  if (status != nullptr) {
    *status = OperationStatus::Ok;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  if (!recording_) {
    if (status != nullptr) {
      *status = OperationStatus::NoActiveSession;
    }
    if (error != nullptr) {
      *error = "No active recording session to stop.";
    }
    return false;
  }

#if RN_PERFETTO_WITH_SDK
  tracing_session_->StopBlocking();
  std::vector<char> trace_bytes = tracing_session_->ReadTraceBlocking();

  if (trace_bytes.empty()) {
    recording_ = false;
    tracing_session_.reset();

    if (status != nullptr) {
      *status = OperationStatus::Internal;
    }
    if (error != nullptr) {
      *error = "Perfetto returned an empty trace payload.";
    }
    return false;
  }

  const auto resolved_path = resolveOutputPath(current_output_path_);
  std::ofstream output(resolved_path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    recording_ = false;
    tracing_session_.reset();

    if (status != nullptr) {
      *status = OperationStatus::Internal;
    }
    if (error != nullptr) {
      *error = "Failed to open output file: " + resolved_path;
    }
    return false;
  }

  output.write(trace_bytes.data(),
               static_cast<std::streamsize>(trace_bytes.size()));
  output.close();

  recording_ = false;
  tracing_session_.reset();

  if (output_path != nullptr) {
    *output_path = resolved_path;
  }

  return true;
#else
  if (status != nullptr) {
    *status = OperationStatus::Unsupported;
  }
  if (error != nullptr) {
    *error = "Perfetto SDK is not available; nothing to stop.";
  }
  if (output_path != nullptr) {
    output_path->clear();
  }
  return false;
#endif
}

void Tracer::beginSectionImpl(const std::string &category,
                              const std::string &name,
                              const std::string &args_json,
                              bool c_api_owned,
                              uint64_t c_api_handle) {
  const std::string event_name = buildEventName(category, name, args_json);
  SectionFrame frame;
  frame.c_api_owned = c_api_owned;
  frame.c_api_handle = c_api_handle;

#if RN_PERFETTO_WITH_SDK
  ensurePerfettoInitialized(false);

  frame.sdk_track_id = nextNonZeroHandle(g_next_sdk_section_track_id);
  TRACE_EVENT_BEGIN("react-native.native",
                    perfetto::DynamicString(event_name.c_str()),
                    perfetto::Track(frame.sdk_track_id));
#endif

#if defined(__ANDROID__)
  ATrace_beginSection(event_name.c_str());
#elif defined(__APPLE__) && TARGET_OS_IOS
  auto *signpost_log = reinterpret_cast<os_log_t>(signpost_log_);
  frame.signpost_id = os_signpost_id_generate(signpost_log);

  os_signpost_interval_begin(signpost_log,
                             frame.signpost_id,
                             "RNPerfettoSection",
                             "%{public}s",
                             event_name.c_str());
#endif

  g_section_stack.push_back(frame);
}

bool Tracer::endSectionImpl(SectionClosePolicy close_policy,
                            uint64_t required_c_api_handle) {
  if (g_section_stack.empty()) {
    return false;
  }

  const SectionFrame &active_frame = g_section_stack.back();
  if (close_policy == SectionClosePolicy::CApiOnly &&
      !active_frame.c_api_owned) {
    return false;
  }

  if (close_policy == SectionClosePolicy::CppOnly && active_frame.c_api_owned) {
    return false;
  }

  if (required_c_api_handle != 0 &&
      active_frame.c_api_handle != required_c_api_handle) {
    return false;
  }

  const SectionFrame frame = active_frame;
  (void)frame;

#if RN_PERFETTO_WITH_SDK
  if (frame.sdk_track_id != 0) {
    TRACE_EVENT_END("react-native.native", perfetto::Track(frame.sdk_track_id));
  }
#endif

#if defined(__ANDROID__)
  ATrace_endSection();
#elif defined(__APPLE__) && TARGET_OS_IOS
  if (frame.signpost_id != OS_SIGNPOST_ID_NULL) {
    auto *signpost_log = reinterpret_cast<os_log_t>(signpost_log_);
    os_signpost_interval_end(signpost_log, frame.signpost_id, "RNPerfettoSection");
  }
#endif

  g_section_stack.pop_back();
  return true;
}

void Tracer::BeginSection(const std::string &category,
                          const std::string &name,
                          const std::string &args_json) {
  beginSectionImpl(category, name, args_json, false, 0);
}

uint64_t Tracer::BeginSectionFromCApi(const std::string &category,
                                      const std::string &name,
                                      const std::string &args_json) {
  const uint64_t c_api_handle = nextNonZeroHandle(g_next_c_api_section_handle);
  beginSectionImpl(category, name, args_json, true, c_api_handle);
  return c_api_handle;
}

void Tracer::EndSection() {
  (void)endSectionImpl(SectionClosePolicy::CppOnly, 0);
}

bool Tracer::EndLastSectionFromCApi() {
  return endSectionImpl(SectionClosePolicy::CApiOnly, 0);
}

bool Tracer::EndSectionFromCApi(uint64_t handle) {
  if (handle == 0) {
    return false;
  }

  return endSectionImpl(SectionClosePolicy::CApiOnly, handle);
}

void Tracer::InstantEvent(const std::string &category,
                          const std::string &name,
                          const std::string &args_json) {
  const std::string event_name = buildEventName(category, name, args_json);

#if RN_PERFETTO_WITH_SDK
  ensurePerfettoInitialized(false);

  TRACE_EVENT_INSTANT("react-native.native",
                      perfetto::DynamicString(event_name.c_str()));
#endif

#if defined(__APPLE__) && TARGET_OS_IOS
  auto *signpost_log = reinterpret_cast<os_log_t>(signpost_log_);
  os_signpost_event_emit(
      signpost_log, OS_SIGNPOST_ID_EXCLUSIVE, "RNPerfettoEvent", "%{public}s", event_name.c_str());
#endif
}

void Tracer::SetCounter(const std::string &category,
                        const std::string &name,
                        double value,
                        const std::string &args_json) {
  const auto counter_name = buildEventName(category, name, args_json);
  (void)value;

#if RN_PERFETTO_WITH_SDK
  ensurePerfettoInitialized(false);

  TRACE_COUNTER("react-native.native",
                perfetto::DynamicString(counter_name.c_str()),
                static_cast<int64_t>(std::llround(value)));
#endif

#if defined(__ANDROID__)
  auto *set_counter = resolveSetCounterFn();
  if (set_counter != nullptr) {
    set_counter(counter_name.c_str(), static_cast<int64_t>(std::llround(value)));
  }
#endif
}

#if RN_PERFETTO_WITH_SDK
void Tracer::ensurePerfettoInitialized(bool enable_system_backend) {
  std::call_once(perfetto_initialize_once_, [enable_system_backend]() {
    perfetto::TracingInitArgs init_args;
    (void)enable_system_backend;
    init_args.backends = perfetto::kInProcessBackend | perfetto::kSystemBackend;

    perfetto::Tracing::Initialize(init_args);
    perfetto::TrackEvent::Register();
  });
}
#endif

} // namespace react_native_perfetto
