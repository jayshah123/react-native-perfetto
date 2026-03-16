#include "rnperfetto/tracer.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <exception>
#include <string>

#include "ReactNativePerfettoTracer.h"

namespace {

rnpt_status_t mapOperationStatus(react_native_perfetto::OperationStatus status) {
  switch (status) {
    case react_native_perfetto::OperationStatus::Ok:
      return RNPT_STATUS_OK;
    case react_native_perfetto::OperationStatus::AlreadyRunning:
    case react_native_perfetto::OperationStatus::NoActiveSession:
      return RNPT_STATUS_FAILED_PRECONDITION;
    case react_native_perfetto::OperationStatus::Unsupported:
      return RNPT_STATUS_UNSUPPORTED;
    case react_native_perfetto::OperationStatus::Internal:
      return RNPT_STATUS_INTERNAL;
  }

  return RNPT_STATUS_INTERNAL;
}

void writeStringOutput(const std::string &value,
                       char *buffer,
                       size_t capacity,
                       size_t *out_length) {
  if (out_length != nullptr) {
    *out_length = value.size();
  }

  if (buffer == nullptr || capacity == 0) {
    return;
  }

  const size_t copy_size = std::min(value.size(), capacity - 1);
  if (copy_size > 0) {
    std::memcpy(buffer, value.data(), copy_size);
  }

  buffer[copy_size] = '\0';
}

std::string nullableString(const char *value) {
  return value == nullptr ? std::string() : std::string(value);
}

} // namespace

extern "C" uint32_t rnpt_abi_version(void) { return RNPT_ABI_VERSION; }

extern "C" rnpt_status_t rnpt_is_sdk_available(uint8_t *out_is_available) {
  if (out_is_available == nullptr) {
    return RNPT_STATUS_INVALID_ARGUMENT;
  }

  try {
    *out_is_available =
        react_native_perfetto::Tracer::Get().IsPerfettoSdkAvailable() ? 1 : 0;
    return RNPT_STATUS_OK;
  } catch (...) {
    return RNPT_STATUS_INTERNAL;
  }
}

extern "C" rnpt_status_t
rnpt_start_recording(const rnpt_recording_config_v1 *config,
                     char *error_buffer,
                     size_t error_buffer_capacity,
                     size_t *error_buffer_length) {
  if (config == nullptr) {
    return RNPT_STATUS_INVALID_ARGUMENT;
  }

  const size_t required_config_size =
      offsetof(rnpt_recording_config_v1, backend_mask) +
      sizeof(config->backend_mask);
  if (config->struct_size < required_config_size) {
    writeStringOutput("Invalid recording config struct size.",
                      error_buffer,
                      error_buffer_capacity,
                      error_buffer_length);
    return RNPT_STATUS_INVALID_ARGUMENT;
  }

  try {
    react_native_perfetto::RecordingConfig native_config;
    native_config.file_path = nullableString(config->file_path);
    if (config->buffer_size_kb > 0) {
      native_config.buffer_size_kb = config->buffer_size_kb;
    }
    if (config->duration_ms > 0) {
      native_config.duration_ms = config->duration_ms;
    }

    constexpr uint32_t kSupportedBackendMask =
        RNPT_BACKEND_IN_PROCESS | RNPT_BACKEND_SYSTEM;
    if ((config->backend_mask & ~kSupportedBackendMask) != 0) {
      writeStringOutput("Unsupported backend_mask bits.",
                        error_buffer,
                        error_buffer_capacity,
                        error_buffer_length);
      return RNPT_STATUS_INVALID_ARGUMENT;
    }

    native_config.enable_in_process_backend =
        (config->backend_mask & RNPT_BACKEND_IN_PROCESS) != 0;
    native_config.enable_system_backend =
        (config->backend_mask & RNPT_BACKEND_SYSTEM) != 0;
    if (config->backend_mask == 0) {
      native_config.enable_in_process_backend = true;
      native_config.enable_system_backend = false;
    }

    std::string error_message;
    react_native_perfetto::OperationStatus native_status =
        react_native_perfetto::OperationStatus::Internal;
    const bool started = react_native_perfetto::Tracer::Get().StartRecording(
        native_config, &error_message, &native_status);

    if (!started) {
      writeStringOutput(
          error_message, error_buffer, error_buffer_capacity, error_buffer_length);
      return mapOperationStatus(native_status);
    }

    writeStringOutput("", error_buffer, error_buffer_capacity, error_buffer_length);
    return RNPT_STATUS_OK;
  } catch (const std::exception &error) {
    writeStringOutput(
        error.what(), error_buffer, error_buffer_capacity, error_buffer_length);
    return RNPT_STATUS_INTERNAL;
  } catch (...) {
    writeStringOutput("Unknown internal error.",
                      error_buffer,
                      error_buffer_capacity,
                      error_buffer_length);
    return RNPT_STATUS_INTERNAL;
  }
}

extern "C" rnpt_status_t
rnpt_stop_recording(char *output_path_buffer,
                    size_t output_path_buffer_capacity,
                    size_t *output_path_buffer_length,
                    char *error_buffer,
                    size_t error_buffer_capacity,
                    size_t *error_buffer_length) {
  try {
    std::string output_path;
    std::string error_message;
    react_native_perfetto::OperationStatus native_status =
        react_native_perfetto::OperationStatus::Internal;
    const bool stopped = react_native_perfetto::Tracer::Get().StopRecording(
        &output_path, &error_message, &native_status);

    if (!stopped) {
      writeStringOutput("",
                        output_path_buffer,
                        output_path_buffer_capacity,
                        output_path_buffer_length);
      writeStringOutput(
          error_message, error_buffer, error_buffer_capacity, error_buffer_length);
      return mapOperationStatus(native_status);
    }

    writeStringOutput(output_path,
                      output_path_buffer,
                      output_path_buffer_capacity,
                      output_path_buffer_length);
    writeStringOutput("", error_buffer, error_buffer_capacity, error_buffer_length);
    return RNPT_STATUS_OK;
  } catch (const std::exception &error) {
    writeStringOutput("",
                      output_path_buffer,
                      output_path_buffer_capacity,
                      output_path_buffer_length);
    writeStringOutput(
        error.what(), error_buffer, error_buffer_capacity, error_buffer_length);
    return RNPT_STATUS_INTERNAL;
  } catch (...) {
    writeStringOutput("",
                      output_path_buffer,
                      output_path_buffer_capacity,
                      output_path_buffer_length);
    writeStringOutput("Unknown internal error.",
                      error_buffer,
                      error_buffer_capacity,
                      error_buffer_length);
    return RNPT_STATUS_INTERNAL;
  }
}

extern "C" rnpt_status_t rnpt_begin_section(const char *category,
                                             const char *name,
                                             const char *args_json,
                                             rnpt_section_handle_t *out_handle) {
  try {
    const rnpt_section_handle_t handle =
        react_native_perfetto::Tracer::Get().BeginSectionFromCApi(
            nullableString(category), nullableString(name), nullableString(args_json));

    if (out_handle != nullptr) {
      *out_handle = handle;
    }

    return RNPT_STATUS_OK;
  } catch (...) {
    return RNPT_STATUS_INTERNAL;
  }
}

extern "C" rnpt_status_t rnpt_end_last_section(void) {
  try {
    return react_native_perfetto::Tracer::Get().EndLastSectionFromCApi()
               ? RNPT_STATUS_OK
               : RNPT_STATUS_FAILED_PRECONDITION;
  } catch (...) {
    return RNPT_STATUS_INTERNAL;
  }
}

extern "C" rnpt_status_t rnpt_end_section(rnpt_section_handle_t handle) {
  if (handle == RNPT_INVALID_SECTION_HANDLE) {
    return RNPT_STATUS_INVALID_ARGUMENT;
  }

  try {
    return react_native_perfetto::Tracer::Get().EndSectionFromCApi(handle)
               ? RNPT_STATUS_OK
               : RNPT_STATUS_FAILED_PRECONDITION;
  } catch (...) {
    return RNPT_STATUS_INTERNAL;
  }
}

extern "C" rnpt_status_t rnpt_instant_event(const char *category,
                                             const char *name,
                                             const char *args_json) {
  try {
    react_native_perfetto::Tracer::Get().InstantEvent(nullableString(category),
                                                      nullableString(name),
                                                      nullableString(args_json));
    return RNPT_STATUS_OK;
  } catch (...) {
    return RNPT_STATUS_INTERNAL;
  }
}

extern "C" rnpt_status_t rnpt_set_counter(const char *category,
                                           const char *name,
                                           double value,
                                           const char *args_json) {
  try {
    react_native_perfetto::Tracer::Get().SetCounter(nullableString(category),
                                                    nullableString(name),
                                                    value,
                                                    nullableString(args_json));
    return RNPT_STATUS_OK;
  } catch (...) {
    return RNPT_STATUS_INTERNAL;
  }
}
