#include "ReactNativePerfettoTracer.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void expect(bool condition, const std::string &message) {
  if (condition) {
    return;
  }

  std::cerr << "[react-native-perfetto][cpp-test] " << message << std::endl;
  std::exit(1);
}

} // namespace

int main() {
  auto &tracer = react_native_perfetto::Tracer::Get();

  expect(!tracer.IsPerfettoSdkAvailable(),
         "Expected RN_PERFETTO_WITH_SDK=0 test build to report SDK unavailable.");

  react_native_perfetto::RecordingConfig config;
  config.file_path = "/tmp/rn-perfetto-test.perfetto-trace";

  std::string start_error;
  const bool started = tracer.StartRecording(config, &start_error);
  expect(!started, "StartRecording should fail in test build without SDK.");
  expect(start_error.find("Perfetto SDK is not bundled") != std::string::npos,
         "StartRecording should provide a clear missing-SDK error message.");

  const bool started_without_error_ptr = tracer.StartRecording(config, nullptr);
  expect(!started_without_error_ptr,
         "StartRecording should still fail when error pointer is null.");

  std::string output_path;
  std::string stop_error;
  const bool stopped = tracer.StopRecording(&output_path, &stop_error);
  expect(!stopped,
         "StopRecording should fail when no active recording session exists.");
  expect(stop_error.find("No active recording session") != std::string::npos,
         "StopRecording should return no-active-session guidance.");

  const bool stopped_without_output = tracer.StopRecording(nullptr, nullptr);
  expect(!stopped_without_output,
         "StopRecording should fail safely when pointers are null.");

  tracer.BeginSection("react-native.test", "section-a", "{\"attempt\":1}");
  tracer.InstantEvent("react-native.test", "event-a", "");
  tracer.SetCounter("react-native.test", "counter-a", 12.3, "");
  tracer.EndSection();

  std::cout << "[react-native-perfetto][cpp-test] all checks passed" << std::endl;
  return 0;
}
