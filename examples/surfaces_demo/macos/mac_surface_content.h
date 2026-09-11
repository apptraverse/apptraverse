#ifndef APPTRAVERSE_SURFACES_MAC_SURFACE_CONTENT_H_
#define APPTRAVERSE_SURFACES_MAC_SURFACE_CONTENT_H_

#import <AppKit/AppKit.h>

#import "mac_surface_actions.h"

// Implemented in Swift (SurfaceContentView.swift, @_cdecl). Kept out of the
// Swift bridging header so Swift does not import its own declaration.
//
// The presenter owns the NSWindow; Swift only fills contentView, so no object
// ownership crosses the boundary and window frame / z-order stay with AppKit.
#ifdef __cplusplus
extern "C" {
#endif

void ApptraverseInstallMacSurfaceContent(NSWindow* window,
                                         id<MacSurfaceActions> actions);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // APPTRAVERSE_SURFACES_MAC_SURFACE_CONTENT_H_
