#ifndef APPTRAVERSE_PLATFORM_APPLE_SWIFT_ENTRY_H_
#define APPTRAVERSE_PLATFORM_APPLE_SWIFT_ENTRY_H_

#include <stdint.h>

// C entry points implemented by the SwiftUI static library. The bridging
// header they import is AppleCatalogBridge.h, not an Æther header.
#ifdef __cplusplus
extern "C" {
#endif

void* ApptraverseMakeCatalogModel(void* projection);
void ApptraverseReleaseCatalogModel(void* model);
void ApptraverseMacInstallCatalogContent(void* window, void* model,
                                         uint64_t window_id);
void* ApptraverseIosMakeRootController(void* model);

#ifdef __cplusplus
}
#endif

#endif  // APPTRAVERSE_PLATFORM_APPLE_SWIFT_ENTRY_H_
