#import <React/RCTBridgeModule.h>

#ifdef RCT_NEW_ARCH_ENABLED
#import <PerfettoSpec/PerfettoSpec.h>
@interface Perfetto : NSObject <NativePerfettoSpec>
#else
@interface Perfetto : NSObject <RCTBridgeModule>
#endif

@end
