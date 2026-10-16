#include "ReactNativePerfettoTracer.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
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

class TraceArgsJsonParser {
 public:
  explicit TraceArgsJsonParser(const std::string &input) : input_(input) {}

  bool Parse(std::vector<TraceArg> *out) {
    skipWhitespace();
    if (!consume('{')) {
      return false;
    }

    skipWhitespace();
    if (consume('}')) {
      return pos_ == input_.size();
    }

    while (true) {
      std::string key;
      if (!parseString(&key)) {
        return false;
      }

      skipWhitespace();
      if (!consume(':')) {
        return false;
      }

      TraceArgValue value;
      if (!parseValue(&value)) {
        return false;
      }

      out->push_back({std::move(key), std::move(value)});

      skipWhitespace();
      if (consume(',')) {
        skipWhitespace();
        continue;
      }

      if (!consume('}')) {
        return false;
      }

      skipWhitespace();
      return pos_ == input_.size();
    }
  }

 private:
  void skipWhitespace() {
    while (pos_ < input_.size() &&
           (input_[pos_] == ' ' || input_[pos_] == '\t' || input_[pos_] == '\n' ||
            input_[pos_] == '\r')) {
      pos_ += 1;
    }
  }

  bool consume(char expected) {
    if (pos_ < input_.size() && input_[pos_] == expected) {
      pos_ += 1;
      return true;
    }

    return false;
  }

  bool parseString(std::string *out) {
    if (!consume('"')) {
      return false;
    }

    std::string result;
    while (pos_ < input_.size()) {
      const char ch = input_[pos_++];
      if (ch == '"') {
        *out = std::move(result);
        return true;
      }

      if (ch == '\\') {
        if (pos_ >= input_.size()) {
          return false;
        }

        const char escape = input_[pos_++];
        switch (escape) {
          case '"':
          case '\\':
          case '/':
            result.push_back(escape);
            break;
          case 'b':
            result.push_back('\b');
            break;
          case 'f':
            result.push_back('\f');
            break;
          case 'n':
            result.push_back('\n');
            break;
          case 'r':
            result.push_back('\r');
            break;
          case 't':
            result.push_back('\t');
            break;
          case 'u': {
            if (pos_ + 4 > input_.size()) {
              return false;
            }
            const auto code = parseHexCodeUnit();
            if (code < 0) {
              return false;
            }
            appendUtf8(static_cast<uint32_t>(code), &result);
            break;
          }
          default:
            return false;
        }
        continue;
      }

      if (static_cast<unsigned char>(ch) < 0x20) {
        return false;
      }

      result.push_back(ch);
    }

    return false;
  }

  int parseHexCodeUnit() {
    int value = 0;
    for (int i = 0; i < 4; ++i) {
      const char ch = input_[pos_++];
      value <<= 4;
      if (ch >= '0' && ch <= '9') {
        value |= ch - '0';
      } else if (ch >= 'a' && ch <= 'f') {
        value |= ch - 'a' + 10;
      } else if (ch >= 'A' && ch <= 'F') {
        value |= ch - 'A' + 10;
      } else {
        return -1;
      }
    }
    return value;
  }

  void appendUtf8(uint32_t code, std::string *out) {
    if (code < 0x80) {
      out->push_back(static_cast<char>(code));
    } else if (code < 0x800) {
      out->push_back(static_cast<char>(0xC0 | (code >> 6)));
      out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
      out->push_back(static_cast<char>(0xE0 | (code >> 12)));
      out->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
      out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
  }

  bool parseLiteral(const char *literal) {
    const size_t length = std::strlen(literal);
    if (input_.compare(pos_, length, literal) == 0) {
      pos_ += length;
      return true;
    }
    return false;
  }

  bool parseValue(TraceArgValue *out) {
    skipWhitespace();
    if (pos_ >= input_.size()) {
      return false;
    }

    const char ch = input_[pos_];
    if (ch == '"') {
      out->type = TraceArgValue::Type::String;
      return parseString(&out->string_value);
    }

    if (ch == 't' || ch == 'f') {
      if (parseLiteral("true")) {
        out->type = TraceArgValue::Type::Bool;
        out->bool_value = true;
        return true;
      }
      if (parseLiteral("false")) {
        out->type = TraceArgValue::Type::Bool;
        out->bool_value = false;
        return true;
      }
      return false;
    }

    if (ch == 'n') {
      if (!parseLiteral("null")) {
        return false;
      }
      out->type = TraceArgValue::Type::Null;
      return true;
    }

    if (ch == '-' || (ch >= '0' && ch <= '9')) {
      return parseNumber(out);
    }

    return false;
  }

  bool parseNumber(TraceArgValue *out) {
    const size_t start = pos_;
    if (input_[pos_] == '-') {
      pos_ += 1;
    }

    while (pos_ < input_.size() &&
           ((input_[pos_] >= '0' && input_[pos_] <= '9') ||
            input_[pos_] == '.' || input_[pos_] == 'e' || input_[pos_] == 'E' ||
            input_[pos_] == '+' || input_[pos_] == '-')) {
      pos_ += 1;
    }

    if (pos_ == start) {
      return false;
    }

    const std::string token = input_.substr(start, pos_ - start);
    char *end = nullptr;
    const double value = std::strtod(token.c_str(), &end);
    if (end == token.c_str() || *end != '\0' || !std::isfinite(value)) {
      return false;
    }

    const auto int_value = static_cast<int64_t>(value);
    if (static_cast<double>(int_value) == value &&
        value >= static_cast<double>(std::numeric_limits<int64_t>::min()) &&
        value <= static_cast<double>(std::numeric_limits<int64_t>::max())) {
      out->type = TraceArgValue::Type::Int;
      out->int_value = int_value;
      return true;
    }

    out->type = TraceArgValue::Type::Double;
    out->double_value = value;
    return true;
  }

  const std::string &input_;
  size_t pos_ = 0;
};

// Returns false when args_json is a non-empty but malformed JSON object.
bool parseTraceArgsJsonDetailed(const std::string &args_json,
                                std::vector<TraceArg> *out) {
  if (args_json.empty()) {
    return true;
  }

  TraceArgsJsonParser parser(args_json);
  return parser.Parse(out);
}

#if RN_PERFETTO_WITH_SDK
void writeTraceArgAnnotations(perfetto::EventContext &ctx,
                              const std::vector<TraceArg> &args) {
  for (const auto &arg : args) {
    // Annotation keys must go through DynamicString so the SDK writes them
    // inline (set_name). The const char* overload interns keys by pointer
    // identity, which is only correct for static string literals — our keys
    // are short-lived std::string storage, so interning them corrupts the
    // iid table once the allocator reuses addresses.
    const perfetto::DynamicString key(arg.key.c_str());
    switch (arg.value.type) {
      case TraceArgValue::Type::Bool:
        ctx.AddDebugAnnotation(key, arg.value.bool_value);
        break;
      case TraceArgValue::Type::Int:
        ctx.AddDebugAnnotation(key, arg.value.int_value);
        break;
      case TraceArgValue::Type::Double:
        ctx.AddDebugAnnotation(key, arg.value.double_value);
        break;
      case TraceArgValue::Type::String:
        ctx.AddDebugAnnotation(key, arg.value.string_value.c_str());
        break;
      case TraceArgValue::Type::Null:
        ctx.AddDebugAnnotation(key, "null");
        break;
    }
  }
}

void writeArgsJsonAnnotations(perfetto::EventContext &ctx,
                              const std::string &args_json) {
  std::vector<TraceArg> args;
  if (parseTraceArgsJsonDetailed(args_json, &args)) {
    writeTraceArgAnnotations(ctx, args);
    return;
  }

  // Malformed args are surfaced instead of silently dropped.
  const std::string truncated = args_json.substr(0, 128);
  ctx.AddDebugAnnotation(perfetto::DynamicString("parse_error"),
                         truncated.c_str());
}
#endif

struct SectionFrame {
  bool c_api_owned = false;
  uint64_t c_api_handle = 0;

#if RN_PERFETTO_WITH_SDK
  std::string sdk_category;
#endif

#if defined(__APPLE__) && TARGET_OS_IOS
  os_signpost_id_t signpost_id = OS_SIGNPOST_ID_NULL;
#endif
};

std::atomic<uint64_t> g_next_c_api_section_handle{1};
thread_local std::vector<SectionFrame> g_section_stack;

uint64_t nextNonZeroHandle(std::atomic<uint64_t> &counter) {
  uint64_t next = counter.fetch_add(1, std::memory_order_relaxed);
  if (next == 0) {
    next = counter.fetch_add(1, std::memory_order_relaxed);
  }
  return next;
}

} // namespace

std::vector<TraceArg> ParseTraceArgsJson(const std::string &args_json) {
  std::vector<TraceArg> args;
  if (!parseTraceArgsJsonDetailed(args_json, &args)) {
    args.clear();
  }

  return args;
}

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

// Produces the "<category>:<name>" slice name used on the SDK path. The
// category prefix keeps slice names self-describing in tools that do not
// surface track event categories.
std::string Tracer::buildSliceName(const std::string &category,
                                   const std::string &name) const {
  const auto resolved_name = sanitizeEventName(name, "unnamed_event");
  const auto resolved_category = sanitizeEventName(category, "react-native");
  return resolved_category + ":" + resolved_name;
}

// Produces the flat "<category>:<name>|args=<json>" name used by the platform
// fallback APIs (ATrace / os_signpost), which can only carry a single string.
std::string Tracer::buildEventName(const std::string &category,
                                   const std::string &name,
                                   const std::string &args_json) const {
  std::string result = buildSliceName(category, name);
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
  // Caller-supplied categories are emitted as dynamic track event categories;
  // the wildcard keeps every caller-defined category enabled.
  track_event_config.add_enabled_categories("*");
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
  [[maybe_unused]] const std::string event_name =
      buildEventName(category, name, args_json);
  SectionFrame frame;
  frame.c_api_owned = c_api_owned;
  frame.c_api_handle = c_api_handle;

#if RN_PERFETTO_WITH_SDK
  ensurePerfettoInitialized(false);

  frame.sdk_category = sanitizeEventName(category, "react-native");
  const std::string slice_name = buildSliceName(category, name);

  // Emit on the current thread's default track so nested sections render as
  // nested slices. Dynamic categories carry the caller-provided category.
  const perfetto::DynamicCategory sdk_category(frame.sdk_category);
  TRACE_EVENT_BEGIN(sdk_category,
                    perfetto::DynamicString(slice_name.c_str()),
                    [&](perfetto::EventContext ctx) {
                      writeArgsJsonAnnotations(ctx, args_json);
                    });
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
  const perfetto::DynamicCategory sdk_category(frame.sdk_category);
  TRACE_EVENT_END(sdk_category);
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
  [[maybe_unused]] const std::string event_name =
      buildEventName(category, name, args_json);

#if RN_PERFETTO_WITH_SDK
  ensurePerfettoInitialized(false);

  const std::string resolved_category =
      sanitizeEventName(category, "react-native");
  const std::string slice_name = buildSliceName(category, name);
  const perfetto::DynamicCategory sdk_category(resolved_category);

  TRACE_EVENT_INSTANT(sdk_category,
                      perfetto::DynamicString(slice_name.c_str()),
                      [&](perfetto::EventContext ctx) {
                        writeArgsJsonAnnotations(ctx, args_json);
                      });
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
  [[maybe_unused]] const auto counter_name =
      buildEventName(category, name, args_json);
  (void)value;

#if RN_PERFETTO_WITH_SDK
  ensurePerfettoInitialized(false);

  // Counter tracks key on the "<category>:<name>" identity, so samples from
  // the same counter land on one track instead of one track per arg set.
  const std::string resolved_category =
      sanitizeEventName(category, "react-native");
  const std::string track_name = buildSliceName(category, name);
  const perfetto::DynamicCategory sdk_category(resolved_category);

  TRACE_COUNTER(sdk_category,
                perfetto::CounterTrack(
                    perfetto::DynamicString(track_name.c_str()))
                    .set_category(resolved_category.c_str()),
                value);
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
