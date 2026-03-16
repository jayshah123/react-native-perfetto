#include "AppUtilitiesJSI.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

#include "../../cpp/ReactNativePerfettoTracer.h"

namespace app_utilities {

namespace {

constexpr const char *kTelemetryCategory = "app.runtime.utilities";

class ScopedOperation {
 public:
  explicit ScopedOperation(std::string operation_name)
      : operation_name_(std::move(operation_name)),
        start_(std::chrono::steady_clock::now()) {
    react_native_perfetto::Tracer::Get().BeginSection(
        kTelemetryCategory, operation_name_, "");
  }

  ~ScopedOperation() {
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_)
            .count();

    react_native_perfetto::Tracer::Get().SetCounter(
        kTelemetryCategory,
        operation_name_ + ".duration_us",
        static_cast<double>(elapsed),
        "");
    react_native_perfetto::Tracer::Get().EndSection();
  }

  void markFailure(const std::string &reason) {
    react_native_perfetto::Tracer::Get().InstantEvent(
        kTelemetryCategory,
        operation_name_ + ".failure",
        reason);
  }

 private:
  std::string operation_name_;
  std::chrono::steady_clock::time_point start_;
};

struct BindingState {
  std::string cache_directory_path;
};

void throwJsiError(facebook::jsi::Runtime &runtime, const std::string &message) {
  throw facebook::jsi::JSError(runtime, message);
}

std::string requireStringArg(facebook::jsi::Runtime &runtime,
                             const facebook::jsi::Value *args,
                             size_t count,
                             size_t index,
                             const char *name) {
  if (index >= count || !args[index].isString()) {
    throwJsiError(runtime, std::string("Expected string argument: ") + name);
  }

  return args[index].asString(runtime).utf8(runtime);
}

double requireNumberArg(facebook::jsi::Runtime &runtime,
                        const facebook::jsi::Value *args,
                        size_t count,
                        size_t index,
                        const char *name) {
  if (index >= count || !args[index].isNumber()) {
    throwJsiError(runtime, std::string("Expected number argument: ") + name);
  }

  return args[index].asNumber();
}

std::string sanitizeKey(facebook::jsi::Runtime &runtime,
                        const std::string &key_value) {
  if (key_value.empty() || key_value.size() > 64) {
    throwJsiError(runtime, "Cache key must be between 1 and 64 characters.");
  }

  for (const char character : key_value) {
    const auto normalized = static_cast<unsigned char>(character);
    const bool valid = std::isalnum(normalized) || character == '_' ||
                       character == '-' || character == '.';
    if (!valid) {
      throwJsiError(runtime,
                    "Cache key can only include letters, digits, _, -, and .");
    }
  }

  return key_value;
}

std::filesystem::path resolveCachePath(facebook::jsi::Runtime &runtime,
                                       const BindingState &state,
                                       const std::string &key_value) {
  const auto safe_key = sanitizeKey(runtime, key_value);
  const auto base_path = std::filesystem::path(state.cache_directory_path);
  return base_path / (safe_key + ".txt");
}

int countPrimes(int limit) {
  if (limit < 2) {
    return 0;
  }

  int count = 0;
  for (int value = 2; value <= limit; ++value) {
    bool prime = true;
    for (int divisor = 2; divisor * divisor <= value; ++divisor) {
      if (value % divisor == 0) {
        prime = false;
        break;
      }
    }

    if (prime) {
      ++count;
    }
  }

  return count;
}

bool isPalindrome(const std::string &value) {
  std::string normalized;
  normalized.reserve(value.size());

  for (const char character : value) {
    const auto normalized_character = static_cast<unsigned char>(character);
    if (!std::isalnum(normalized_character)) {
      continue;
    }

    normalized.push_back(
        static_cast<char>(std::tolower(normalized_character)));
  }

  return std::equal(
      normalized.begin(),
      normalized.begin() + static_cast<std::ptrdiff_t>(normalized.size() / 2),
      normalized.rbegin());
}

void writeTextInternal(facebook::jsi::Runtime &runtime,
                       const BindingState &state,
                       const std::string &key_value,
                       const std::string &contents) {
  if (contents.size() > 100000) {
    throwJsiError(runtime,
                  "contents must be <= 100000 UTF-8 bytes for this demo.");
  }

  std::filesystem::create_directories(state.cache_directory_path);
  const auto resolved_path = resolveCachePath(runtime, state, key_value);

  std::ofstream output(resolved_path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    throwJsiError(runtime, "Unable to open cache file for writing.");
  }

  output << contents;
  output.close();
}

std::optional<std::string> readTextInternal(facebook::jsi::Runtime &runtime,
                                            const BindingState &state,
                                            const std::string &key_value) {
  const auto resolved_path = resolveCachePath(runtime, state, key_value);

  std::ifstream input(resolved_path, std::ios::binary);
  if (!input.is_open()) {
    return std::nullopt;
  }

  std::stringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

bool removeInternal(facebook::jsi::Runtime &runtime,
                    const BindingState &state,
                    const std::string &key_value) {
  const auto resolved_path = resolveCachePath(runtime, state, key_value);

  std::error_code error_code;
  const bool removed = std::filesystem::remove(resolved_path, error_code);

  if (error_code) {
    throwJsiError(runtime, "Unable to remove cache entry.");
  }

  return removed;
}

facebook::jsi::Function makeAddFunction(facebook::jsi::Runtime &runtime) {
  return facebook::jsi::Function::createFromHostFunction(
      runtime,
      facebook::jsi::PropNameID::forAscii(runtime, "add"),
      2,
      [](facebook::jsi::Runtime &runtime,
         const facebook::jsi::Value &,
         const facebook::jsi::Value *args,
         size_t count) -> facebook::jsi::Value {
        ScopedOperation operation("utils.math.add");

        const auto left = requireNumberArg(runtime, args, count, 0, "left");
        const auto right = requireNumberArg(runtime, args, count, 1, "right");

        return facebook::jsi::Value(left + right);
      });
}

facebook::jsi::Function makeCountPrimesFunction(facebook::jsi::Runtime &runtime) {
  return facebook::jsi::Function::createFromHostFunction(
      runtime,
      facebook::jsi::PropNameID::forAscii(runtime, "countPrimes"),
      1,
      [](facebook::jsi::Runtime &runtime,
         const facebook::jsi::Value &,
         const facebook::jsi::Value *args,
         size_t count) -> facebook::jsi::Value {
        ScopedOperation operation("utils.math.count_primes");

        const auto limit_value = requireNumberArg(runtime, args, count, 0, "limit");
        const auto limit = static_cast<int>(limit_value);
        if (limit < 0 || limit > 250000) {
          operation.markFailure("out_of_range");
          throwJsiError(runtime, "limit must be between 0 and 250000.");
        }

        const auto result = countPrimes(limit);
        return facebook::jsi::Value(result);
      });
}

facebook::jsi::Function makeIsPalindromeFunction(facebook::jsi::Runtime &runtime) {
  return facebook::jsi::Function::createFromHostFunction(
      runtime,
      facebook::jsi::PropNameID::forAscii(runtime, "isPalindrome"),
      1,
      [](facebook::jsi::Runtime &runtime,
         const facebook::jsi::Value &,
         const facebook::jsi::Value *args,
         size_t count) -> facebook::jsi::Value {
        ScopedOperation operation("utils.logic.is_palindrome");

        const auto candidate = requireStringArg(runtime, args, count, 0, "value");
        const auto result = isPalindrome(candidate);
        return facebook::jsi::Value(result);
      });
}

facebook::jsi::Function makeWriteTextFunction(
    facebook::jsi::Runtime &runtime,
    const std::shared_ptr<BindingState> &state) {
  return facebook::jsi::Function::createFromHostFunction(
      runtime,
      facebook::jsi::PropNameID::forAscii(runtime, "writeText"),
      2,
      [state](facebook::jsi::Runtime &runtime,
              const facebook::jsi::Value &,
              const facebook::jsi::Value *args,
              size_t count) -> facebook::jsi::Value {
        ScopedOperation operation("utils.cache.write_text");

        const auto key_value = requireStringArg(runtime, args, count, 0, "key");
        const auto contents = requireStringArg(runtime, args, count, 1, "contents");

        try {
          writeTextInternal(runtime, *state, key_value, contents);
        } catch (...) {
          operation.markFailure("write_failed");
          throw;
        }

        return facebook::jsi::Value::undefined();
      });
}

facebook::jsi::Function makeReadTextFunction(
    facebook::jsi::Runtime &runtime,
    const std::shared_ptr<BindingState> &state) {
  return facebook::jsi::Function::createFromHostFunction(
      runtime,
      facebook::jsi::PropNameID::forAscii(runtime, "readText"),
      1,
      [state](facebook::jsi::Runtime &runtime,
              const facebook::jsi::Value &,
              const facebook::jsi::Value *args,
              size_t count) -> facebook::jsi::Value {
        ScopedOperation operation("utils.cache.read_text");

        const auto key_value = requireStringArg(runtime, args, count, 0, "key");

        try {
          const auto contents = readTextInternal(runtime, *state, key_value);
          if (!contents.has_value()) {
            return facebook::jsi::Value::null();
          }

          return facebook::jsi::String::createFromUtf8(runtime, *contents);
        } catch (...) {
          operation.markFailure("read_failed");
          throw;
        }
      });
}

facebook::jsi::Function makeRemoveFunction(
    facebook::jsi::Runtime &runtime,
    const std::shared_ptr<BindingState> &state) {
  return facebook::jsi::Function::createFromHostFunction(
      runtime,
      facebook::jsi::PropNameID::forAscii(runtime, "remove"),
      1,
      [state](facebook::jsi::Runtime &runtime,
              const facebook::jsi::Value &,
              const facebook::jsi::Value *args,
              size_t count) -> facebook::jsi::Value {
        ScopedOperation operation("utils.cache.remove");

        const auto key_value = requireStringArg(runtime, args, count, 0, "key");

        try {
          return facebook::jsi::Value(removeInternal(runtime, *state, key_value));
        } catch (...) {
          operation.markFailure("remove_failed");
          throw;
        }
      });
}

facebook::jsi::Function makeCaptureTraceFunction(
    facebook::jsi::Runtime &runtime,
    const std::shared_ptr<BindingState> &state) {
  return facebook::jsi::Function::createFromHostFunction(
      runtime,
      facebook::jsi::PropNameID::forAscii(runtime, "captureTrace"),
      0,
      [state](facebook::jsi::Runtime &runtime,
              const facebook::jsi::Value &,
              const facebook::jsi::Value *,
              size_t) -> facebook::jsi::Value {
        auto &tracer = react_native_perfetto::Tracer::Get();
        if (!tracer.IsPerfettoSdkAvailable()) {
          throwJsiError(runtime, "Diagnostics capture is unavailable.");
        }

        react_native_perfetto::RecordingConfig config;
        config.enable_in_process_backend = true;
        config.enable_system_backend = false;

        std::string start_error;
        if (!tracer.StartRecording(config, &start_error)) {
          throwJsiError(runtime,
                        start_error.empty()
                            ? "Unable to start diagnostics capture."
                            : start_error);
        }

        try {
          {
            ScopedOperation operation("utils.math.add");
            const auto result = 17.0 + 25.0;
            (void)result;
          }

          {
            ScopedOperation operation("utils.math.count_primes");
            const auto result = countPrimes(20000);
            (void)result;
          }

          {
            ScopedOperation operation("utils.logic.is_palindrome");
            const auto result = isPalindrome("Never odd or even");
            (void)result;
          }

          const auto timestamp_ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
          const auto payload = "captured at " + std::to_string(timestamp_ms);

          {
            ScopedOperation operation("utils.cache.write_text");
            try {
              writeTextInternal(runtime, *state, "sample-note", payload);
            } catch (...) {
              operation.markFailure("write_failed");
              throw;
            }
          }

          {
            ScopedOperation operation("utils.cache.read_text");
            try {
              const auto read_back =
                  readTextInternal(runtime, *state, "sample-note");
              (void)read_back;
            } catch (...) {
              operation.markFailure("read_failed");
              throw;
            }
          }

          {
            ScopedOperation operation("utils.cache.remove");
            try {
              const auto removed = removeInternal(runtime, *state, "sample-note");
              (void)removed;
            } catch (...) {
              operation.markFailure("remove_failed");
              throw;
            }
          }
        } catch (...) {
          std::string ignored_output_path;
          std::string ignored_error;
          tracer.StopRecording(&ignored_output_path, &ignored_error);
          throw;
        }

        std::string trace_path;
        std::string stop_error;
        if (!tracer.StopRecording(&trace_path, &stop_error)) {
          throwJsiError(runtime,
                        stop_error.empty()
                            ? "Unable to stop diagnostics capture."
                            : stop_error);
        }

        return facebook::jsi::String::createFromUtf8(runtime, trace_path);
      });
}

} // namespace

void InstallAppUtilities(facebook::jsi::Runtime &runtime,
                         std::string cache_directory_path) {
  auto global = runtime.global();

  if (global.hasProperty(runtime, "__appUtilities")) {
    return;
  }

  auto state = std::make_shared<BindingState>();
  state->cache_directory_path = std::move(cache_directory_path);

  facebook::jsi::Object utilities(runtime);

  facebook::jsi::Object math(runtime);
  math.setProperty(runtime, "add", makeAddFunction(runtime));
  math.setProperty(runtime, "countPrimes", makeCountPrimesFunction(runtime));
  utilities.setProperty(runtime, "math", std::move(math));

  facebook::jsi::Object logic(runtime);
  logic.setProperty(runtime, "isPalindrome", makeIsPalindromeFunction(runtime));
  utilities.setProperty(runtime, "logic", std::move(logic));

  facebook::jsi::Object cache(runtime);
  cache.setProperty(runtime, "writeText", makeWriteTextFunction(runtime, state));
  cache.setProperty(runtime, "readText", makeReadTextFunction(runtime, state));
  cache.setProperty(runtime, "remove", makeRemoveFunction(runtime, state));
  utilities.setProperty(runtime, "cache", std::move(cache));

  facebook::jsi::Object diagnostics(runtime);
  diagnostics.setProperty(runtime,
                          "captureTrace",
                          makeCaptureTraceFunction(runtime, state));
  utilities.setProperty(runtime, "diagnostics", std::move(diagnostics));

  global.setProperty(runtime, "__appUtilities", std::move(utilities));

  react_native_perfetto::Tracer::Get().InstantEvent(
      kTelemetryCategory,
      "utils.install.complete",
      "");
}

} // namespace app_utilities
