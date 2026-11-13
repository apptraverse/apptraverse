#import <Foundation/Foundation.h>

#import "apptraverse/platform/macos/ATMacApplicationBridge.h"

NS_ASSUME_NONNULL_BEGIN
FOUNDATION_EXPORT void ATRegisterMinimalMacApplication(void);

FOUNDATION_EXPORT ATMacApplicationBridge*
ATCreateMinimalApplicationBridge(
    NSString* userDirectory,
    id<ATMacApplicationBridgeDelegate> delegate);

NS_ASSUME_NONNULL_END
