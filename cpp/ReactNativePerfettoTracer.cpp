#include "ReactNativePerfettoTracer.h"

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

#if defined(__APPLE__) && TARGET_OS_IOS
thread_local std::vector<os_signpost_id_t> g_signpost_stack;
#endif

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

bool Tracer::StartRecording(const RecordingConfig &config, std::string *error) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (recording_) {
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
  next_section_track_id_ = 1;
  active_section_track_ids_.clear();
  recording_ = true;
  return true;
#else
  (void)config;
  if (error != nullptr) {
    *error =
        "Perfetto SDK is not bundled. Add sdk/perfetto.h and sdk/perfetto.cc "
        "under cpp/third_party/perfetto/ to enable recording.";
  }
  return false;
#endif
}

bool Tracer::StopRecording(std::string *output_path, std::string *error) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!recording_) {
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

    if (error != nullptr) {
      *error = "Failed to open output file: " + resolved_path;
    }
    return false;
  }

  output.write(trace_bytes.data(),
               static_cast<std::streamsize>(trace_bytes.size()));
  output.close();

  active_section_track_ids_.clear();
  recording_ = false;
  tracing_session_.reset();

  if (output_path != nullptr) {
    *output_path = resolved_path;
  }

  return true;
#else
  if (error != nullptr) {
    *error = "Perfetto SDK is not available; nothing to stop.";
  }
  if (output_path != nullptr) {
    output_path->clear();
  }
  return false;
#endif
}

void Tracer::BeginSection(const std::string &category,
                          const std::string &name,
                          const std::string &args_json) {
  const std::string event_name = buildEventName(category, name, args_json);

#if RN_PERFETTO_WITH_SDK
  uint64_t section_track_id = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    section_track_id = next_section_track_id_++;
    active_section_track_ids_.push_back(section_track_id);
  }

  TRACE_EVENT_BEGIN("react-native.native",
                    perfetto::DynamicString(event_name.c_str()),
                    perfetto::Track(section_track_id));
#endif

#if defined(__ANDROID__)
  ATrace_beginSection(event_name.c_str());
#elif defined(__APPLE__) && TARGET_OS_IOS
  auto *signpost_log = reinterpret_cast<os_log_t>(signpost_log_);
  const auto signpost_id = os_signpost_id_generate(signpost_log);

  os_signpost_interval_begin(signpost_log,
                             signpost_id,
                             "RNPerfettoSection",
                             "%{public}s",
                             event_name.c_str());

  g_signpost_stack.push_back(signpost_id);
#endif
}

void Tracer::EndSection() {
#if RN_PERFETTO_WITH_SDK
  uint64_t section_track_id = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_section_track_ids_.empty()) {
      section_track_id = active_section_track_ids_.back();
      active_section_track_ids_.pop_back();
    }
  }

  if (section_track_id != 0) {
    TRACE_EVENT_END("react-native.native", perfetto::Track(section_track_id));
  }
#endif

#if defined(__ANDROID__)
  ATrace_endSection();
#elif defined(__APPLE__) && TARGET_OS_IOS
  if (g_signpost_stack.empty()) {
    return;
  }

  auto *signpost_log = reinterpret_cast<os_log_t>(signpost_log_);
  const auto signpost_id = g_signpost_stack.back();
  g_signpost_stack.pop_back();

  os_signpost_interval_end(signpost_log, signpost_id, "RNPerfettoSection");
#endif
}

void Tracer::InstantEvent(const std::string &category,
                          const std::string &name,
                          const std::string &args_json) {
  const std::string event_name = buildEventName(category, name, args_json);

#if RN_PERFETTO_WITH_SDK
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

#if RN_PERFETTO_WITH_SDK
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
  if (perfetto_initialized_) {
    return;
  }

  perfetto::TracingInitArgs init_args;
  (void)enable_system_backend;
  init_args.backends = perfetto::kInProcessBackend | perfetto::kSystemBackend;

  perfetto::Tracing::Initialize(init_args);
  perfetto::TrackEvent::Register();

  perfetto_initialized_ = true;
}
#endif

} // namespace react_native_perfetto
