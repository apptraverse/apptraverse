#import <Foundation/Foundation.h>
#import "apptraverse/platform/ios/ATIosApplicationBridge.h"

NS_ASSUME_NONNULL_BEGIN
FOUNDATION_EXPORT void ATRegisterWindowCatalogIosApplication(void);
@interface ATWindowCatalogIosApplication : NSObject
- (instancetype)initWithUserDirectory:(NSString*)directory;
- (ATIosApplicationBridge*)makeBridge:
    (id<ATIosApplicationBridgeDelegate>)delegate;
@end
NS_ASSUME_NONNULL_END
