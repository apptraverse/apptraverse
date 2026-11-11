#import <Foundation/Foundation.h>
#import "window_catalog_composition.h"

// Test-only input at the public presenter/proxy boundary. Used to exercise an
// explicit model frame request independently of native resize notifications.
FOUNDATION_EXPORT void ATMacAcceptanceRequestFrame(
    uint64_t presenterToken, int32_t x, int32_t y,
    int32_t width, int32_t height);
FOUNDATION_EXPORT BOOL ATMacAcceptanceCheckLateNativeReports(void);
