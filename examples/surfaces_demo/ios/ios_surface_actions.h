#ifndef APPTRAVERSE_SURFACES_IOS_SURFACE_ACTIONS_H_
#define APPTRAVERSE_SURFACES_IOS_SURFACE_ACTIONS_H_

#import <Foundation/Foundation.h>

// Swift-visible boundary of the iOS host: pure Objective-C only. Swift's clang
// importer is the Xcode one and cannot parse this project's C++, so the host
// stays behind this protocol. SurfacesRootViewController adopts it and forwards
// to IOSApp (native input → host → current presenter → model event).
@protocol IOSSurfaceActions <NSObject>
- (void)addSurface;
- (void)removeCurrentSurface;
@end

#endif  // APPTRAVERSE_SURFACES_IOS_SURFACE_ACTIONS_H_
