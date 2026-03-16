#import "Perfetto.h"

#ifdef RCT_NEW_ARCH_ENABLED
#import <PerfettoSpec/PerfettoSpec.h>
#endif

#include <array>
#include <cmath>
#include <limits>
#include <string>

#import "rnperfetto/tracer.h"

namespace {

NSNumber *PerfettoIsPerfettoSdkAvailable() {
  uint8_t is_available = 0;
  const auto status = rnpt_is_sdk_available(&is_available);
  if (status != RNPT_STATUS_OK) {
    return @(NO);
  }

  return @(is_available != 0);
}

std::string toStdString(NSString *value) {
  if (value == nil || value.length == 0) {
    return "";
  }

  const char *utf8 = value.UTF8String;
  if (utf8 == nullptr) {
    return "";
  }

  return std::string(utf8);
}

uint32_t clampToUint32(double value) {
  if (!std::isfinite(value) || value <= 0) {
    return 0;
  }

  const double max = static_cast<double>(std::numeric_limits<uint32_t>::max());
  if (value >= max) {
    return std::numeric_limits<uint32_t>::max();
  }

  return static_cast<uint32_t>(value);
}

NSString *fallbackStatusMessage(rnpt_status_t status) {
  switch (status) {
    case RNPT_STATUS_INVALID_ARGUMENT:
      return @"Invalid tracing arguments.";
    case RNPT_STATUS_FAILED_PRECONDITION:
      return @"Tracing operation is not valid in the current state.";
    case RNPT_STATUS_UNSUPPORTED:
      return @"Perfetto SDK is unavailable in this build.";
    case RNPT_STATUS_INTERNAL:
      return @"Tracing operation failed due to an internal error.";
    case RNPT_STATUS_OK:
      break;
  }

  return @"Tracing operation failed.";
}

NSString *resolveErrorMessage(rnpt_status_t status,
                              const std::array<char, 512> &error_buffer,
                              size_t error_length) {
  if (error_length > 0 && error_buffer[0] != '\0') {
    return [NSString stringWithUTF8String:error_buffer.data()];
  }

  return fallbackStatusMessage(status);
}

void logNonOkStatus(const char *operation, rnpt_status_t status) {
  if (status == RNPT_STATUS_OK) {
    return;
  }

  NSLog(@"[RNPerfetto] %s returned status=%d", operation, (int)status);
}

void PerfettoStartRecording(NSString *filePath,
                            double bufferSizeKb,
                            double durationMs,
                            NSString *backend,
                            RCTPromiseResolveBlock resolve,
                            RCTPromiseRejectBlock reject) {
  NSString *resolvedPath = filePath;
  if (resolvedPath.length == 0) {
    NSString *name = [NSString stringWithFormat:@"rn-perfetto-%.0f.perfetto-trace", [[NSDate date] timeIntervalSince1970] * 1000.0];
    resolvedPath = [NSTemporaryDirectory() stringByAppendingPathComponent:name];
  }

  const std::string file_path = toStdString(resolvedPath);
  rnpt_recording_config_v1 config{};
  config.struct_size = sizeof(config);
  config.file_path = file_path.c_str();
  config.buffer_size_kb = clampToUint32(bufferSizeKb);
  config.duration_ms = clampToUint32(durationMs);
  config.backend_mask = RNPT_BACKEND_IN_PROCESS;

  NSString *normalizedBackend = [backend.lowercaseString
      stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
  if ([normalizedBackend isEqualToString:@"system"]) {
    config.backend_mask = RNPT_BACKEND_SYSTEM;
  }

  std::array<char, 512> error_buffer = {0};
  size_t error_length = 0;
  const auto status = rnpt_start_recording(
      &config, error_buffer.data(), error_buffer.size(), &error_length);
  if (status != RNPT_STATUS_OK) {
    reject(@"ERR_PERFETTO_START",
           resolveErrorMessage(status, error_buffer, error_length),
           nil);
    return;
  }

  resolve(@(YES));
}

void PerfettoStopRecording(RCTPromiseResolveBlock resolve,
                           RCTPromiseRejectBlock reject) {
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
    reject(@"ERR_PERFETTO_STOP",
           resolveErrorMessage(status, error_buffer, error_length),
           nil);
    return;
  }

  if (output_path_length >= output_path_buffer.size()) {
    reject(@"ERR_PERFETTO_STOP",
           @"Trace output path exceeded native output buffer.",
           nil);
    return;
  }

  resolve([NSString stringWithUTF8String:output_path_buffer.data()]);
}

void PerfettoBeginSection(NSString *category,
                          NSString *name,
                          NSString *argsJson) {
  const auto category_value = toStdString(category);
  const auto name_value = toStdString(name);
  const auto args_value = toStdString(argsJson);
  const auto status = rnpt_begin_section(category_value.c_str(),
                                         name_value.c_str(),
                                         args_value.c_str(),
                                         nullptr);
  logNonOkStatus("rnpt_begin_section", status);
}

void PerfettoEndSection() {
  const auto status = rnpt_end_last_section();
  logNonOkStatus("rnpt_end_last_section", status);
}

void PerfettoInstantEvent(NSString *category,
                          NSString *name,
                          NSString *argsJson) {
  const auto category_value = toStdString(category);
  const auto name_value = toStdString(name);
  const auto args_value = toStdString(argsJson);
  const auto status = rnpt_instant_event(
      category_value.c_str(), name_value.c_str(), args_value.c_str());
  logNonOkStatus("rnpt_instant_event", status);
}

void PerfettoSetCounter(NSString *category,
                        NSString *name,
                        double value,
                        NSString *argsJson) {
  const auto category_value = toStdString(category);
  const auto name_value = toStdString(name);
  const auto args_value = toStdString(argsJson);
  const auto status = rnpt_set_counter(
      category_value.c_str(), name_value.c_str(), value, args_value.c_str());
  logNonOkStatus("rnpt_set_counter", status);
}

} // namespace

@implementation Perfetto

RCT_EXPORT_MODULE(Perfetto)

#ifndef RCT_NEW_ARCH_ENABLED
RCT_EXPORT_BLOCKING_SYNCHRONOUS_METHOD(isPerfettoSdkAvailable) {
  return PerfettoIsPerfettoSdkAvailable();
}

RCT_REMAP_METHOD(startRecording,
                 startRecording:(NSString *)filePath
                 bufferSizeKb:(nonnull NSNumber *)bufferSizeKb
                 durationMs:(nonnull NSNumber *)durationMs
                 backend:(NSString *)backend
                 resolve:(RCTPromiseResolveBlock)resolve
                 reject:(RCTPromiseRejectBlock)reject) {
  PerfettoStartRecording(filePath,
                         bufferSizeKb.doubleValue,
                         durationMs.doubleValue,
                         backend,
                         resolve,
                         reject);
}

RCT_REMAP_METHOD(stopRecording,
                 stopRecording:(RCTPromiseResolveBlock)resolve
                 reject:(RCTPromiseRejectBlock)reject) {
  PerfettoStopRecording(resolve, reject);
}

RCT_EXPORT_METHOD(beginSection:(NSString *)category
                  name:(NSString *)name
                  argsJson:(NSString *)argsJson) {
  PerfettoBeginSection(category, name, argsJson);
}

RCT_EXPORT_METHOD(endSection) {
  PerfettoEndSection();
}

RCT_EXPORT_METHOD(instantEvent:(NSString *)category
                  name:(NSString *)name
                  argsJson:(NSString *)argsJson) {
  PerfettoInstantEvent(category, name, argsJson);
}

RCT_EXPORT_METHOD(setCounter:(NSString *)category
                  name:(NSString *)name
                  value:(double)value
                  argsJson:(NSString *)argsJson) {
  PerfettoSetCounter(category, name, value, argsJson);
}
#endif

#ifdef RCT_NEW_ARCH_ENABLED
- (NSNumber *)isPerfettoSdkAvailable {
  return PerfettoIsPerfettoSdkAvailable();
}

- (void)startRecording:(NSString *)filePath
          bufferSizeKb:(double)bufferSizeKb
            durationMs:(double)durationMs
               backend:(NSString *)backend
               resolve:(RCTPromiseResolveBlock)resolve
                reject:(RCTPromiseRejectBlock)reject {
  PerfettoStartRecording(filePath,
                         bufferSizeKb,
                         durationMs,
                         backend,
                         resolve,
                         reject);
}

- (void)stopRecording:(RCTPromiseResolveBlock)resolve
               reject:(RCTPromiseRejectBlock)reject {
  PerfettoStopRecording(resolve, reject);
}

- (void)beginSection:(NSString *)category
                name:(NSString *)name
            argsJson:(NSString *)argsJson {
  PerfettoBeginSection(category, name, argsJson);
}

- (void)endSection {
  PerfettoEndSection();
}

- (void)instantEvent:(NSString *)category
                name:(NSString *)name
            argsJson:(NSString *)argsJson {
  PerfettoInstantEvent(category, name, argsJson);
}

- (void)setCounter:(NSString *)category
              name:(NSString *)name
             value:(double)value
          argsJson:(NSString *)argsJson {
  PerfettoSetCounter(category, name, value, argsJson);
}

- (std::shared_ptr<facebook::react::TurboModule>)getTurboModule:
    (const facebook::react::ObjCTurboModule::InitParams &)params {
  return std::make_shared<facebook::react::NativePerfettoSpecJSI>(params);
}
#endif

+ (BOOL)requiresMainQueueSetup {
  return NO;
}

@end
