#import <Foundation/Foundation.h>

#import "apptraverse/platform/ios/ATIosApplicationBridge.h"

NS_ASSUME_NONNULL_BEGIN
FOUNDATION_EXPORT void ATRegisterMinimalIosApplication(void);

FOUNDATION_EXPORT ATIosApplicationBridge*
ATCreateMinimalApplicationBridge(
    NSString* userDirectory,
    id<ATIosApplicationBridgeDelegate> delegate);

NS_ASSUME_NONNULL_END
