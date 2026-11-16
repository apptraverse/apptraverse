#ifndef APPTRAVERSE_SURFACES_MAC_SURFACE_ACTIONS_H_
#define APPTRAVERSE_SURFACES_MAC_SURFACE_ACTIONS_H_

#import <Foundation/Foundation.h>

// Swift-visible boundary of the macOS host: pure Objective-C only. Swift's
// clang importer is the Xcode one and cannot parse this project's C++, so the
// presenter stays behind this protocol. SurfaceWindowDelegate adopts it and
// forwards to MacSurfacePresenter (native input → presenter → model event).
@protocol MacSurfaceActions <NSObject>
- (void)addSurface;
- (void)closeThisWindow;
@end

#endif  // APPTRAVERSE_SURFACES_MAC_SURFACE_ACTIONS_H_
