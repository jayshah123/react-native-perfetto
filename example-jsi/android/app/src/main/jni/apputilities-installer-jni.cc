#include <jni.h>

#include <string>

#include <jsi/jsi.h>

#include "AppUtilitiesJSI.h"

namespace {

std::string toStdString(JNIEnv *env, jstring value) {
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

} // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_apputilities_example_bindings_AppUtilitiesInstallerModule_nativeInstall(
    JNIEnv *env,
    jobject,
    jlong runtime_pointer,
    jstring cache_directory_path) {
  auto *runtime = reinterpret_cast<facebook::jsi::Runtime *>(runtime_pointer);
  if (runtime == nullptr) {
    return JNI_FALSE;
  }

  app_utilities::InstallAppUtilities(*runtime, toStdString(env, cache_directory_path));
  return JNI_TRUE;
}
