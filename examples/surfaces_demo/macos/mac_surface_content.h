#ifndef APPTRAVERSE_SURFACES_MAC_SURFACE_CONTENT_H_
#define APPTRAVERSE_SURFACES_MAC_SURFACE_CONTENT_H_

#import <AppKit/AppKit.h>

#import "mac_surface_actions.h"

// Implemented in Swift (SurfaceContentView.swift, @_cdecl). Kept out of the
// Swift bridging header so Swift does not import its own declaration.
//
// The presenter owns the NSWindow; Swift only fills contentView, so no object
// ownership crosses the boundary and window frame / z-order stay with AppKit.
// `is_wide` is SurfacePresenter::IsWide after model publication — not a live
// contentView measurement shortcut.
#ifdef __cplusplus
extern "C" {
#endif

void ApptraverseInstallMacSurfaceContent(NSWindow* window,
                                         id<MacSurfaceActions> actions,
                                         BOOL is_wide);

void ApptraverseUpdateMacSurfaceContent(NSWindow* window, BOOL is_wide);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // APPTRAVERSE_SURFACES_MAC_SURFACE_CONTENT_H_
