#import "AppUtilitiesInstaller.h"

#import <React/RCTBridge+Private.h>

#include <jsi/jsi.h>

#include "../../cpp/AppUtilitiesJSI.h"

@implementation AppUtilitiesInstaller

@synthesize bridge = _bridge;

RCT_EXPORT_MODULE();

RCT_EXPORT_BLOCKING_SYNCHRONOUS_METHOD(install) {
  RCTCxxBridge *cxxBridge = (RCTCxxBridge *)self.bridge;
  if (cxxBridge == nil || cxxBridge.runtime == nullptr) {
    return @NO;
  }

  auto *runtime = reinterpret_cast<facebook::jsi::Runtime *>(cxxBridge.runtime);
  if (runtime == nullptr) {
    return @NO;
  }

  NSString *cacheDirectoryPath =
      NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES).firstObject;
  if (cacheDirectoryPath.length == 0) {
    cacheDirectoryPath = NSTemporaryDirectory();
  }

  app_utilities::InstallAppUtilities(*runtime, std::string(cacheDirectoryPath.UTF8String));
  return @YES;
}

+ (BOOL)requiresMainQueueSetup {
  return NO;
}

@end
