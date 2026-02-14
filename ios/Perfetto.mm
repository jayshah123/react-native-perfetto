#import "Perfetto.h"

#import <React/RCTBridgeModule.h>

#import "../cpp/ReactNativePerfettoTracer.h"

@implementation Perfetto

- (NSNumber *)isPerfettoSdkAvailable {
  return @(react_native_perfetto::Tracer::Get().IsPerfettoSdkAvailable());
}

- (void)startRecording:(NSString *)filePath
          bufferSizeKb:(double)bufferSizeKb
            durationMs:(double)durationMs
               backend:(NSString *)backend
               resolve:(RCTPromiseResolveBlock)resolve
                reject:(RCTPromiseRejectBlock)reject {
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

- (void)stopRecording:(RCTPromiseResolveBlock)resolve
               reject:(RCTPromiseRejectBlock)reject {
  std::string outputPath;
  std::string error;

  const bool stopped = react_native_perfetto::Tracer::Get().StopRecording(&outputPath, &error);
  if (!stopped) {
    reject(@"ERR_PERFETTO_STOP", [NSString stringWithUTF8String:error.c_str()], nil);
    return;
  }

  resolve([NSString stringWithUTF8String:outputPath.c_str()]);
}

- (void)beginSection:(NSString *)category
                name:(NSString *)name
            argsJson:(NSString *)argsJson {
  react_native_perfetto::Tracer::Get().BeginSection(
      std::string(category.UTF8String),
      std::string(name.UTF8String),
      std::string(argsJson.UTF8String));
}

- (void)endSection {
  react_native_perfetto::Tracer::Get().EndSection();
}

- (void)instantEvent:(NSString *)category
                name:(NSString *)name
            argsJson:(NSString *)argsJson {
  react_native_perfetto::Tracer::Get().InstantEvent(
      std::string(category.UTF8String),
      std::string(name.UTF8String),
      std::string(argsJson.UTF8String));
}

- (void)setCounter:(NSString *)category
              name:(NSString *)name
             value:(double)value
          argsJson:(NSString *)argsJson {
  react_native_perfetto::Tracer::Get().SetCounter(
      std::string(category.UTF8String),
      std::string(name.UTF8String),
      value,
      std::string(argsJson.UTF8String));
}

- (std::shared_ptr<facebook::react::TurboModule>)getTurboModule:
    (const facebook::react::ObjCTurboModule::InitParams &)params {
  return std::make_shared<facebook::react::NativePerfettoSpecJSI>(params);
}

+ (NSString *)moduleName {
  return @"Perfetto";
}

@end
