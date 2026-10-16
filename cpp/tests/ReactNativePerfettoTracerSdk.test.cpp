// SDK-enabled coverage for the tracer core. Compiled with
// RN_PERFETTO_WITH_SDK=1 and the vendored perfetto amalgamation; complements
// ReactNativePerfettoTracer.test.cpp which covers the no-SDK path.

#include "ReactNativePerfettoTracer.h"
#include "rnperfetto/tracer.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void expect(bool condition, const std::string &message) {
  if (condition) {
    return;
  }

  std::cerr << "[react-native-perfetto][cpp-sdk-test] " << message << std::endl;
  std::exit(1);
}

std::string readFileBytes(const std::string &path) {
  std::ifstream stream(path, std::ios::binary);
  std::ostringstream contents;
  contents << stream.rdbuf();
  return contents.str();
}

bool containsBytes(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

void expectParseFailure(const std::string &json, const std::string &label) {
  const auto args = react_native_perfetto::ParseTraceArgsJson(json);
  expect(args.empty(), label + " should not produce args.");
}

} // namespace

int main() {
  auto &tracer = react_native_perfetto::Tracer::Get();

  expect(tracer.IsPerfettoSdkAvailable(),
         "SDK test build should report the Perfetto SDK as available.");

  // --- ParseTraceArgsJson unit coverage ---

  expectParseFailure("", "empty input");
  expectParseFailure("not json", "non-JSON input");
  expectParseFailure("[1,2,3]", "top-level array");
  expectParseFailure("{\"a\":1", "unterminated object");
  expectParseFailure("{\"a\":{\"b\":1}}", "nested object");
  expectParseFailure("{\"a\":[1]}", "array value");

  {
    const auto args = react_native_perfetto::ParseTraceArgsJson(
        "{\"attempt\":3,\"ratio\":2.5,\"label\":\"warm up\",\"flag\":true,"
        "\"off\":false,\"note\":null,\"escaped\":\"a\\nb\"}");

    expect(args.size() == 7, "Parser should emit all seven args.");
    expect(args[0].key == "attempt" &&
               args[0].value.type ==
                   react_native_perfetto::TraceArgValue::Type::Int &&
               args[0].value.int_value == 3,
           "attempt should parse as int 3.");
    expect(args[1].value.type ==
                   react_native_perfetto::TraceArgValue::Type::Double &&
               args[1].value.double_value == 2.5,
           "ratio should parse as double 2.5.");
    expect(args[2].value.type ==
                   react_native_perfetto::TraceArgValue::Type::String &&
               args[2].value.string_value == "warm up",
           "label should parse as a string.");
    expect(args[3].value.type ==
                   react_native_perfetto::TraceArgValue::Type::Bool &&
               args[3].value.bool_value,
           "flag should parse as true.");
    expect(args[4].value.type ==
                   react_native_perfetto::TraceArgValue::Type::Bool &&
               !args[4].value.bool_value,
           "off should parse as false.");
    expect(args[5].value.type ==
               react_native_perfetto::TraceArgValue::Type::Null,
           "note should parse as null.");
    expect(args[6].value.string_value == "a\nb",
           "escaped newline should decode.");
  }

  // --- Recording + structured emission coverage ---

  react_native_perfetto::RecordingConfig config;
  config.file_path = "/tmp/rn-perfetto-sdk-test.perfetto-trace";
  config.buffer_size_kb = 1024;

  std::string start_error;
  expect(tracer.StartRecording(config, &start_error),
         "StartRecording should succeed with the SDK: " + start_error);

  tracer.BeginSection("test.category",
                      "sdk-outer",
                      "{\"attempt\":3,\"label\":\"warmup\"}");
  tracer.BeginSection("test.category", "sdk-inner", "");
  tracer.EndSection();
  tracer.EndSection();
  tracer.InstantEvent("test.category", "sdk-event", "{\"phase\":\"run\"}");
  tracer.InstantEvent("test.category", "sdk-bad-args", "{not valid json");
  tracer.SetCounter("test.category", "sdk-counter", 7.5, "");

  rnpt_section_handle_t c_section = RNPT_INVALID_SECTION_HANDLE;
  expect(rnpt_begin_section("test.category",
                            "c-api-section",
                            "{\"origin\":\"c-abi\"}",
                            &c_section) == RNPT_STATUS_OK,
         "rnpt_begin_section should open a section under the SDK.");
  expect(rnpt_end_section(c_section) == RNPT_STATUS_OK,
         "rnpt_end_section should close the C API section.");

  std::string output_path;
  std::string stop_error;
  expect(tracer.StopRecording(&output_path, &stop_error),
         "StopRecording should succeed with the SDK: " + stop_error);
  expect(!output_path.empty(), "StopRecording should report the trace path.");

  const std::string trace = readFileBytes(output_path);
  expect(!trace.empty(), "Trace output should be non-empty.");

  expect(containsBytes(trace, "sdk-outer"),
         "Trace should contain the outer slice name.");
  expect(containsBytes(trace, "sdk-inner"),
         "Trace should contain the nested inner slice name.");
  expect(containsBytes(trace, "sdk-event"),
         "Trace should contain the instant event name.");
  expect(containsBytes(trace, "sdk-counter"),
         "Trace should contain the counter track name.");

  expect(containsBytes(trace, "test.category"),
         "Trace should carry the caller-provided dynamic category name.");

  expect(containsBytes(trace, "attempt"),
         "Section arg key should be stored as a debug annotation.");
  expect(containsBytes(trace, "warmup"),
         "Section arg string value should be stored in the trace.");
  expect(containsBytes(trace, "phase"),
         "Instant event arg key should be stored as a debug annotation.");
  expect(containsBytes(trace, "run"),
         "Instant event arg value should be stored in the trace.");
  expect(containsBytes(trace, "origin"),
         "C API section arg key should be stored as a debug annotation.");
  expect(containsBytes(trace, "c-abi"),
         "C API section arg value should be stored in the trace.");
  expect(containsBytes(trace, "parse_error"),
         "Malformed args JSON should surface a parse_error debug annotation.");

  expect(!containsBytes(trace, "|args="),
         "Flattened fallback naming must not leak into SDK slice names.");

  std::cout << "[react-native-perfetto][cpp-sdk-test] all checks passed"
            << std::endl;
  return 0;
}
