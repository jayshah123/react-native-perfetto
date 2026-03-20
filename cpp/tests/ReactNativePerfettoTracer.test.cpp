#include "ReactNativePerfettoTracer.h"
#include "rnperfetto/tracer.h"

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

  expect(rnpt_abi_version() == RNPT_ABI_VERSION,
         "C ABI version should match RNPT_ABI_VERSION constant.");

  uint8_t sdk_available = 1;
  expect(rnpt_is_sdk_available(&sdk_available) == RNPT_STATUS_OK,
         "rnpt_is_sdk_available should succeed with an output pointer.");
  expect(sdk_available == 0,
         "rnpt_is_sdk_available should report unavailable SDK in test build.");

  rnpt_recording_config_v1 c_config{};
  c_config.struct_size = sizeof(c_config);
  c_config.file_path = "/tmp/rn-perfetto-c-api-test.perfetto-trace";
  c_config.buffer_size_kb = 1024;
  c_config.duration_ms = 0;
  c_config.backend_mask = RNPT_BACKEND_IN_PROCESS;

  char c_start_error[512] = {0};
  size_t c_start_error_length = 0;
  const rnpt_status_t c_start_status =
      rnpt_start_recording(&c_config,
                           c_start_error,
                           sizeof(c_start_error),
                           &c_start_error_length);
  expect(c_start_status == RNPT_STATUS_UNSUPPORTED,
         "rnpt_start_recording should report unsupported without SDK.");
  expect(std::string(c_start_error).find("Perfetto SDK is not bundled") !=
             std::string::npos,
         "rnpt_start_recording should return the missing-SDK error message.");
  expect(c_start_error_length > 0,
         "rnpt_start_recording should report error text length.");

  char c_stop_path[512] = {0};
  char c_stop_error[512] = {0};
  size_t c_stop_path_length = 0;
  size_t c_stop_error_length = 0;
  const rnpt_status_t c_stop_status =
      rnpt_stop_recording(c_stop_path,
                          sizeof(c_stop_path),
                          &c_stop_path_length,
                          c_stop_error,
                          sizeof(c_stop_error),
                          &c_stop_error_length);
  expect(c_stop_status == RNPT_STATUS_FAILED_PRECONDITION,
         "rnpt_stop_recording should fail when no recording is active.");
  expect(std::string(c_stop_error).find("No active recording session") !=
             std::string::npos,
         "rnpt_stop_recording should provide no-active-session guidance.");
  expect(c_stop_path_length == 0,
         "rnpt_stop_recording should clear output path length on failure.");
  expect(c_stop_error_length > 0,
         "rnpt_stop_recording should report error text length.");

  rnpt_section_handle_t section_one = RNPT_INVALID_SECTION_HANDLE;
  rnpt_section_handle_t section_two = RNPT_INVALID_SECTION_HANDLE;
  expect(rnpt_begin_section("react-native.test",
                            "c-api-section-one",
                            "",
                            &section_one) == RNPT_STATUS_OK,
         "rnpt_begin_section should open the first section.");
  expect(rnpt_begin_section("react-native.test",
                            "c-api-section-two",
                            "",
                            &section_two) == RNPT_STATUS_OK,
         "rnpt_begin_section should open the second section.");
  expect(section_one != RNPT_INVALID_SECTION_HANDLE,
         "rnpt_begin_section should provide a valid section handle.");
  expect(section_two != RNPT_INVALID_SECTION_HANDLE,
         "rnpt_begin_section should provide a valid section handle.");

  expect(rnpt_end_section(section_one) == RNPT_STATUS_FAILED_PRECONDITION,
         "rnpt_end_section should reject out-of-order section handles.");
  expect(rnpt_end_section(section_two) == RNPT_STATUS_OK,
         "rnpt_end_section should close the active top section.");
  expect(rnpt_end_last_section() == RNPT_STATUS_OK,
         "rnpt_end_last_section should close the remaining section.");
  expect(rnpt_end_last_section() == RNPT_STATUS_FAILED_PRECONDITION,
         "rnpt_end_last_section should fail when no sections remain.");

  rnpt_section_handle_t mixed_handle_one = RNPT_INVALID_SECTION_HANDLE;
  expect(rnpt_begin_section("react-native.test",
                            "c-api-mixed-one",
                            "",
                            &mixed_handle_one) == RNPT_STATUS_OK,
         "rnpt_begin_section should open mixed-test section #1.");
  tracer.BeginSection("react-native.test", "cpp-mixed-one", "");
  expect(rnpt_end_section(mixed_handle_one) == RNPT_STATUS_FAILED_PRECONDITION,
         "rnpt_end_section should not close through a non-C top section.");
  tracer.EndSection();
  expect(rnpt_end_section(mixed_handle_one) == RNPT_STATUS_OK,
         "rnpt_end_section should succeed once the C section is back on top.");

  rnpt_section_handle_t mixed_handle_two = RNPT_INVALID_SECTION_HANDLE;
  expect(rnpt_begin_section("react-native.test",
                            "c-api-mixed-two",
                            "",
                            &mixed_handle_two) == RNPT_STATUS_OK,
         "rnpt_begin_section should open mixed-test section #2.");
  tracer.BeginSection("react-native.test", "cpp-mixed-two", "");
  expect(rnpt_end_last_section() == RNPT_STATUS_FAILED_PRECONDITION,
         "rnpt_end_last_section should reject a non-C top section.");
  tracer.EndSection();
  expect(rnpt_end_last_section() == RNPT_STATUS_OK,
         "rnpt_end_last_section should succeed when C section is top-most.");
  expect(mixed_handle_two != RNPT_INVALID_SECTION_HANDLE,
         "mixed_handle_two should remain valid for diagnostics.");

  rnpt_section_handle_t mixed_handle_three = RNPT_INVALID_SECTION_HANDLE;
  expect(rnpt_begin_section("react-native.test",
                            "c-api-mixed-three",
                            "",
                            &mixed_handle_three) == RNPT_STATUS_OK,
         "rnpt_begin_section should open mixed-test section #3.");
  tracer.EndSection();
  expect(rnpt_end_section(mixed_handle_three) == RNPT_STATUS_OK,
         "Tracer::EndSection should not close a C-API-owned section.");

  expect(rnpt_instant_event("react-native.test", "c-api-event", "") ==
             RNPT_STATUS_OK,
         "rnpt_instant_event should succeed.");
  expect(rnpt_set_counter("react-native.test", "c-api-counter", 42.0, "") ==
             RNPT_STATUS_OK,
         "rnpt_set_counter should succeed.");

  std::cout << "[react-native-perfetto][cpp-test] all checks passed" << std::endl;
  return 0;
}
