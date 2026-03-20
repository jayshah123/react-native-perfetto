#include <jni.h>

#include <string>

#include "ReactNativePerfettoTracer.h"

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

} // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_com_perfetto_PerfettoModule_nativeIsPerfettoSdkAvailable(JNIEnv *, jobject) {
  return react_native_perfetto::Tracer::Get().IsPerfettoSdkAvailable() ? JNI_TRUE
                                                                        : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_perfetto_PerfettoModule_nativeStartRecording(JNIEnv *env,
                                                      jobject,
                                                      jstring file_path,
                                                      jint buffer_size_kb,
                                                      jint duration_ms,
                                                      jstring backend) {
  react_native_perfetto::RecordingConfig config;
  config.file_path = jstringToStdString(env, file_path);
  config.buffer_size_kb = buffer_size_kb > 0 ? static_cast<uint32_t>(buffer_size_kb)
                                              : config.buffer_size_kb;
  config.duration_ms = duration_ms > 0 ? static_cast<uint32_t>(duration_ms) : 0;

  const auto backend_value = jstringToStdString(env, backend);
  config.enable_system_backend = backend_value == "system";
  config.enable_in_process_backend = !config.enable_system_backend;

  std::string error;
  const bool started =
      react_native_perfetto::Tracer::Get().StartRecording(config, &error);

  if (!started) {
    throwJavaIllegalState(env, error);
    return JNI_FALSE;
  }

  return JNI_TRUE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_perfetto_PerfettoModule_nativeStopRecording(JNIEnv *env, jobject) {
  std::string output_path;
  std::string error;

  const bool stopped = react_native_perfetto::Tracer::Get().StopRecording(
      &output_path, &error);

  if (!stopped) {
    throwJavaIllegalState(env, error);
    return nullptr;
  }

  return toJString(env, output_path);
}

extern "C" JNIEXPORT void JNICALL
Java_com_perfetto_PerfettoModule_nativeBeginSection(JNIEnv *env,
                                                    jobject,
                                                    jstring category,
                                                    jstring name,
                                                    jstring args_json) {
  react_native_perfetto::Tracer::Get().BeginSection(jstringToStdString(env, category),
                                                    jstringToStdString(env, name),
                                                    jstringToStdString(env, args_json));
}

extern "C" JNIEXPORT void JNICALL
Java_com_perfetto_PerfettoModule_nativeEndSection(JNIEnv *, jobject) {
  react_native_perfetto::Tracer::Get().EndSection();
}

extern "C" JNIEXPORT void JNICALL
Java_com_perfetto_PerfettoModule_nativeInstantEvent(JNIEnv *env,
                                                    jobject,
                                                    jstring category,
                                                    jstring name,
                                                    jstring args_json) {
  react_native_perfetto::Tracer::Get().InstantEvent(jstringToStdString(env, category),
                                                    jstringToStdString(env, name),
                                                    jstringToStdString(env, args_json));
}

extern "C" JNIEXPORT void JNICALL
Java_com_perfetto_PerfettoModule_nativeSetCounter(JNIEnv *env,
                                                  jobject,
                                                  jstring category,
                                                  jstring name,
                                                  jdouble value,
                                                  jstring args_json) {
  react_native_perfetto::Tracer::Get().SetCounter(jstringToStdString(env, category),
                                                  jstringToStdString(env, name),
                                                  value,
                                                  jstringToStdString(env, args_json));
}
