#import <Foundation/Foundation.h>
#import "apptraverse/platform/macos/ATMacApplicationBridge.h"

NS_ASSUME_NONNULL_BEGIN
FOUNDATION_EXPORT void ATRegisterWindowCatalogApplication(void);
FOUNDATION_EXPORT BOOL ATDistillWindowCatalogApplication(NSString* directory);
FOUNDATION_EXPORT ATMacApplicationBridge* ATCreateWindowCatalogApplicationBridge(
    NSString* userDirectory, id<ATMacApplicationBridgeDelegate> delegate);
NS_ASSUME_NONNULL_END
