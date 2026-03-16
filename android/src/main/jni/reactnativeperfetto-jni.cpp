#include <jni.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

#include <android/log.h>

#include "rnperfetto/tracer.h"

namespace {

std::string jstringToStdString(JNIEnv *env, jstring value) {
  if (value == nullptr) {
    return "";
  }

  const char *chars = env->GetStringUTFChars(value, nullptr);
  if (chars == nullptr) {
    return "";
  }

  std::string result(chars);
  env->ReleaseStringUTFChars(value, chars);
  return result;
}

jstring toJString(JNIEnv *env, const std::string &value) {
  return env->NewStringUTF(value.c_str());
}

void throwJavaIllegalState(JNIEnv *env, const std::string &message) {
  jclass exception_class = env->FindClass("java/lang/IllegalStateException");
  if (exception_class != nullptr) {
    env->ThrowNew(exception_class, message.c_str());
  }
}

std::string fallbackStatusMessage(rnpt_status_t status) {
  switch (status) {
    case RNPT_STATUS_INVALID_ARGUMENT:
      return "Invalid tracing arguments.";
    case RNPT_STATUS_FAILED_PRECONDITION:
      return "Tracing operation is not valid in the current state.";
    case RNPT_STATUS_UNSUPPORTED:
      return "Perfetto SDK is unavailable in this build.";
    case RNPT_STATUS_INTERNAL:
      return "Tracing operation failed due to an internal error.";
    case RNPT_STATUS_OK:
      break;
  }

  return "Tracing operation failed.";
}

std::string resolveErrorMessage(rnpt_status_t status,
                                const std::array<char, 512> &error_buffer,
                                size_t error_length) {
  if (error_length > 0 && error_buffer[0] != '\0') {
    return std::string(error_buffer.data());
  }

  return fallbackStatusMessage(status);
}

void logNonOkStatus(const char *operation, rnpt_status_t status) {
  if (status == RNPT_STATUS_OK) {
    return;
  }

  const int priority =
      status == RNPT_STATUS_INTERNAL ? ANDROID_LOG_ERROR : ANDROID_LOG_WARN;
  __android_log_print(priority,
                      "RNPerfetto",
                      "%s returned status=%d",
                      operation,
                      static_cast<int>(status));
}

} // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_com_perfetto_PerfettoModule_nativeIsPerfettoSdkAvailable(JNIEnv *, jobject) {
  uint8_t is_available = 0;
  const auto status = rnpt_is_sdk_available(&is_available);
  if (status != RNPT_STATUS_OK) {
    return JNI_FALSE;
  }

  return is_available == 0 ? JNI_FALSE : JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_perfetto_PerfettoModule_nativeStartRecording(JNIEnv *env,
                                                      jobject,
                                                      jstring file_path,
                                                      jint buffer_size_kb,
                                                      jint duration_ms,
                                                      jstring backend) {
  const std::string file_path_value = jstringToStdString(env, file_path);
  rnpt_recording_config_v1 config{};
  config.struct_size = sizeof(config);
  config.file_path = file_path_value.c_str();
  config.buffer_size_kb =
      buffer_size_kb > 0 ? static_cast<uint32_t>(buffer_size_kb) : 0;
  config.duration_ms = duration_ms > 0 ? static_cast<uint32_t>(duration_ms) : 0;
  config.backend_mask = RNPT_BACKEND_IN_PROCESS;

  auto backend_value = jstringToStdString(env, backend);
  std::transform(backend_value.begin(),
                 backend_value.end(),
                 backend_value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  if (backend_value == "system") {
    config.backend_mask = RNPT_BACKEND_SYSTEM;
  }

  std::array<char, 512> error_buffer = {0};
  size_t error_length = 0;
  const auto status = rnpt_start_recording(&config,
                                           error_buffer.data(),
                                           error_buffer.size(),
                                           &error_length);

  if (status != RNPT_STATUS_OK) {
    throwJavaIllegalState(
        env, resolveErrorMessage(status, error_buffer, error_length));
    return JNI_FALSE;
  }

  return JNI_TRUE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_perfetto_PerfettoModule_nativeStopRecording(JNIEnv *env, jobject) {
  std::array<char, 16384> output_path_buffer = {0};
  size_t output_path_length = 0;
  std::array<char, 512> error_buffer = {0};
  size_t error_length = 0;

  const auto status = rnpt_stop_recording(output_path_buffer.data(),
                                          output_path_buffer.size(),
                                          &output_path_length,
                                          error_buffer.data(),
                                          error_buffer.size(),
                                          &error_length);

  if (status != RNPT_STATUS_OK) {
    throwJavaIllegalState(
        env, resolveErrorMessage(status, error_buffer, error_length));
    return nullptr;
  }

  if (output_path_length >= output_path_buffer.size()) {
    throwJavaIllegalState(env,
                          "Trace output path exceeded native output buffer.");
    return nullptr;
  }

  return toJString(env, output_path_buffer.data());
}

extern "C" JNIEXPORT void JNICALL
Java_com_perfetto_PerfettoModule_nativeBeginSection(JNIEnv *env,
                                                    jobject,
                                                    jstring category,
                                                    jstring name,
                                                    jstring args_json) {
  const auto category_value = jstringToStdString(env, category);
  const auto name_value = jstringToStdString(env, name);
  const auto args_value = jstringToStdString(env, args_json);
  const auto status = rnpt_begin_section(category_value.c_str(),
                                         name_value.c_str(),
                                         args_value.c_str(),
                                         nullptr);
  logNonOkStatus("rnpt_begin_section", status);
}

extern "C" JNIEXPORT void JNICALL
Java_com_perfetto_PerfettoModule_nativeEndSection(JNIEnv *, jobject) {
  const auto status = rnpt_end_last_section();
  logNonOkStatus("rnpt_end_last_section", status);
}

extern "C" JNIEXPORT void JNICALL
Java_com_perfetto_PerfettoModule_nativeInstantEvent(JNIEnv *env,
                                                    jobject,
                                                    jstring category,
                                                    jstring name,
                                                    jstring args_json) {
  const auto category_value = jstringToStdString(env, category);
  const auto name_value = jstringToStdString(env, name);
  const auto args_value = jstringToStdString(env, args_json);
  const auto status = rnpt_instant_event(category_value.c_str(),
                                         name_value.c_str(),
                                         args_value.c_str());
  logNonOkStatus("rnpt_instant_event", status);
}

extern "C" JNIEXPORT void JNICALL
Java_com_perfetto_PerfettoModule_nativeSetCounter(JNIEnv *env,
                                                  jobject,
                                                  jstring category,
                                                  jstring name,
                                                  jdouble value,
                                                  jstring args_json) {
  const auto category_value = jstringToStdString(env, category);
  const auto name_value = jstringToStdString(env, name);
  const auto args_value = jstringToStdString(env, args_json);
  const auto status = rnpt_set_counter(category_value.c_str(),
                                       name_value.c_str(),
                                       value,
                                       args_value.c_str());
  logNonOkStatus("rnpt_set_counter", status);
}
