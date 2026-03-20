#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(RNPT_BUILDING_SHARED)
#define RNPT_EXPORT __declspec(dllexport)
#else
#define RNPT_EXPORT __declspec(dllimport)
#endif
#else
#define RNPT_EXPORT __attribute__((visibility("default")))
#endif

#define RNPT_ABI_VERSION 1u

#define RNPT_BACKEND_IN_PROCESS (1u << 0)
#define RNPT_BACKEND_SYSTEM (1u << 1)

typedef uint64_t rnpt_section_handle_t;
#define RNPT_INVALID_SECTION_HANDLE ((rnpt_section_handle_t)0)

typedef enum rnpt_status_t {
  RNPT_STATUS_OK = 0,
  RNPT_STATUS_INVALID_ARGUMENT = 1,
  RNPT_STATUS_FAILED_PRECONDITION = 2,
  RNPT_STATUS_UNSUPPORTED = 3,
  RNPT_STATUS_INTERNAL = 4,
} rnpt_status_t;

typedef struct rnpt_recording_config_v1 {
  size_t struct_size;
  const char *file_path;
  uint32_t buffer_size_kb;
  uint32_t duration_ms;
  uint32_t backend_mask;
} rnpt_recording_config_v1;

#if defined(__cplusplus)
extern "C" {
#endif

RNPT_EXPORT uint32_t rnpt_abi_version(void);

RNPT_EXPORT rnpt_status_t rnpt_is_sdk_available(uint8_t *out_is_available);

RNPT_EXPORT rnpt_status_t
rnpt_start_recording(const rnpt_recording_config_v1 *config,
                     char *error_buffer,
                     size_t error_buffer_capacity,
                     size_t *error_buffer_length);

RNPT_EXPORT rnpt_status_t
rnpt_stop_recording(char *output_path_buffer,
                    size_t output_path_buffer_capacity,
                    size_t *output_path_buffer_length,
                    char *error_buffer,
                    size_t error_buffer_capacity,
                    size_t *error_buffer_length);

RNPT_EXPORT rnpt_status_t rnpt_begin_section(const char *category,
                                             const char *name,
                                             const char *args_json,
                                             rnpt_section_handle_t *out_handle);

// Ends the most recently opened C-ABI section on the current thread.
// Returns RNPT_STATUS_FAILED_PRECONDITION when the top section belongs to a
// non-C-ABI caller or when no sections are open on this thread.
RNPT_EXPORT rnpt_status_t rnpt_end_last_section(void);

// Ends a specific section handle using same-thread, strict LIFO semantics.
// Returns RNPT_STATUS_FAILED_PRECONDITION when |handle| is not the current
// thread's top C-ABI section handle.
RNPT_EXPORT rnpt_status_t rnpt_end_section(rnpt_section_handle_t handle);

RNPT_EXPORT rnpt_status_t rnpt_instant_event(const char *category,
                                             const char *name,
                                             const char *args_json);

RNPT_EXPORT rnpt_status_t rnpt_set_counter(const char *category,
                                           const char *name,
                                           double value,
                                           const char *args_json);

#if defined(__cplusplus)
}
#endif
