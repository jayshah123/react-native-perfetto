#import "Perfetto.h"

#ifdef RCT_NEW_ARCH_ENABLED
#import <PerfettoSpec/PerfettoSpec.h>
#endif

#import "../cpp/ReactNativePerfettoTracer.h"

namespace {

NSNumber *PerfettoIsPerfettoSdkAvailable() {
  return @(react_native_perfetto::Tracer::Get().IsPerfettoSdkAvailable());
}

void PerfettoStartRecording(NSString *filePath,
                            double bufferSizeKb,
                            double durationMs,
                            NSString *backend,
                            RCTPromiseResolveBlock resolve,
                            RCTPromiseRejectBlock reject) {
  react_native_perfetto::RecordingConfig config;

  NSString *resolvedPath = filePath;
  if (resolvedPath.length == 0) {
    NSString *name = [NSString stringWithFormat:@"rn-perfetto-%.0f.perfetto-trace", [[NSDate date] timeIntervalSince1970] * 1000.0];
    resolvedPath = [NSTemporaryDirectory() stringByAppendingPathComponent:name];
  }

  config.file_path = std::string(resolvedPath.UTF8String);
  if (bufferSizeKb > 0) {
    config.buffer_size_kb = static_cast<uint32_t>(bufferSizeKb);
  }
  if (durationMs > 0) {
    config.duration_ms = static_cast<uint32_t>(durationMs);
  }

  NSString *normalizedBackend = [backend lowercaseString];
  config.enable_system_backend = [normalizedBackend isEqualToString:@"system"];
  config.enable_in_process_backend = !config.enable_system_backend;

  std::string error;
  const bool started = react_native_perfetto::Tracer::Get().StartRecording(config, &error);
  if (!started) {
    reject(@"ERR_PERFETTO_START", [NSString stringWithUTF8String:error.c_str()], nil);
    return;
  }

  resolve(@(YES));
}

void PerfettoStopRecording(RCTPromiseResolveBlock resolve,
                           RCTPromiseRejectBlock reject) {
  std::string outputPath;
  std::string error;

  const bool stopped = react_native_perfetto::Tracer::Get().StopRecording(&outputPath, &error);
  if (!stopped) {
    reject(@"ERR_PERFETTO_STOP", [NSString stringWithUTF8String:error.c_str()], nil);
    return;
  }

  resolve([NSString stringWithUTF8String:outputPath.c_str()]);
}

void PerfettoBeginSection(NSString *category,
                          NSString *name,
                          NSString *argsJson) {
  react_native_perfetto::Tracer::Get().BeginSection(
      std::string(category.UTF8String),
      std::string(name.UTF8String),
      std::string(argsJson.UTF8String));
}

void PerfettoEndSection() {
  react_native_perfetto::Tracer::Get().EndSection();
}

void PerfettoInstantEvent(NSString *category,
                          NSString *name,
                          NSString *argsJson) {
  react_native_perfetto::Tracer::Get().InstantEvent(
      std::string(category.UTF8String),
      std::string(name.UTF8String),
      std::string(argsJson.UTF8String));
}

void PerfettoSetCounter(NSString *category,
                        NSString *name,
                        double value,
                        NSString *argsJson) {
  react_native_perfetto::Tracer::Get().SetCounter(
      std::string(category.UTF8String),
      std::string(name.UTF8String),
      value,
      std::string(argsJson.UTF8String));
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
