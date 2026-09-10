#import <UIKit/UIKit.h>

#include <filesystem>
#include <string>

#include "apptraverse/object_macros.h"

#include "ios_app.h"
#include "ios_presenters.h"

namespace {

// App-local writable state. Tests may point the run at their own directory
// with `--state-dir <path>`.
std::filesystem::path ResolveStateDir() {
  NSArray<NSString*>* arguments = [[NSProcessInfo processInfo] arguments];
  for (NSUInteger i = 0; i + 1 < [arguments count]; ++i) {
    if ([arguments[i] isEqualToString:@"--state-dir"]) {
      return std::filesystem::path{[arguments[i + 1] UTF8String]};
    }
  }
  NSArray<NSString*>* support = NSSearchPathForDirectoriesInDomains(
      NSApplicationSupportDirectory, NSUserDomainMask, YES);
  std::filesystem::path state_dir{[support[0] UTF8String]};
  state_dir /= "AppTraverseSurfaces";
  std::filesystem::create_directories(state_dir);
  return state_dir;
}

}  // namespace

@interface SurfacesAppDelegate : NSObject <UIApplicationDelegate>
@end

@implementation SurfacesAppDelegate {
  apptraverse::IOSApp app_;
}

- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)options {
  (void)application;
  (void)options;
  app_.Start(ResolveStateDir());
  return YES;
}

- (void)applicationWillTerminate:(UIApplication*)application {
  (void)application;
  app_.Stop();
}

@end

int main(int argc, char* argv[]) {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSurfacesModelRegistration();
  apptraverse::EnsureIOSSurfacePresenterRegistration();
  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil,
                             NSStringFromClass([SurfacesAppDelegate class]));
  }
}
