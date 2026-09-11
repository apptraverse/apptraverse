#ifndef APPTRAVERSE_SURFACES_IOS_SURFACE_CONTENT_H_
#define APPTRAVERSE_SURFACES_IOS_SURFACE_CONTENT_H_

#import <UIKit/UIKit.h>

#import "ios_surface_actions.h"

// Implemented in Swift (SurfaceContentView.swift, @_cdecl). Kept out of the
// Swift bridging header so Swift does not import its own declaration.
//
// UIKit keeps every container UIView and its frame: page order comes from
// Surfaces::surfaces, the current page from Surfaces::mobile_current, and the
// bar geometry from viewDidLayoutSubviews. Swift only fills a container.
#ifdef __cplusplus
extern "C" {
#endif

// `hue`/`title` are derived from the Surface, so the page carries no state.
void ApptraverseInstallIOSSurfacePage(UIView* container, NSString* title,
                                      double hue);

void ApptraverseInstallIOSSurfaceBar(UIView* container,
                                     id<IOSSurfaceActions> actions,
                                     BOOL can_remove);

// Re-supplies the model-derived Remove state; Swift holds none of its own.
void ApptraverseUpdateIOSSurfaceBar(UIView* container, BOOL can_remove);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // APPTRAVERSE_SURFACES_IOS_SURFACE_CONTENT_H_
