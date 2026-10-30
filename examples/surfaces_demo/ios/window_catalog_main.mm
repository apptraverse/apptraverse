#import <UIKit/UIKit.h>

#include <filesystem>
#include <memory>
#include <vector>

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/ios/ios_application_host.h"

#include "window_catalog_lifecycle.h"
#include "window_catalog_model.h"

namespace {

std::filesystem::path DefaultStateDir() {
  NSArray<NSString*>* paths = NSSearchPathForDirectoriesInDomains(
      NSApplicationSupportDirectory, NSUserDomainMask, YES);
  NSString* base = paths.firstObject;
  if (base == nil) {
    return "AppTraverseWindowCatalogExperiment";
  }
  return std::filesystem::path{base.UTF8String} /
         "AppTraverseWindowCatalogExperiment";
}

std::filesystem::path StateDirFromProcess() {
  std::filesystem::path state_dir = DefaultStateDir();
  NSArray<NSString*>* args = NSProcessInfo.processInfo.arguments;
  for (NSUInteger i = 0; i < args.count; ++i) {
    if ([args[i] isEqualToString:@"--state-dir"] && i + 1 < args.count) {
      state_dir = args[i + 1].UTF8String;
    }
  }
  std::error_code created;
  std::filesystem::create_directories(state_dir, created);
  if (created) {
    apptraverse::WriteFatalStderr("fatal: cannot create catalog profile directory\n");
    std::abort();
  }
  return state_dir;
}

struct IosWindowCatalogDriver {
  apptraverse::WindowCatalogModelSession session;
  std::unique_ptr<apptraverse::ios::IosApplicationHost> host;

  void Start() {
    if (host) {
      return;
    }
    session.state_dir = StateDirFromProcess();
    apptraverse::ios::IosApplicationHostCallbacks callbacks;
    callbacks.run_model =
        [this](apptraverse::ApplicationPublishedCallback on_published) {
          session.Run(std::move(on_published));
        };
    callbacks.request_stop = [this] { session.runtime.RequestStop(); };
    callbacks.request_persist = [this](std::function<void(int)> done) {
      if (!session.request_persist) {
        done(1);
        return;
      }
      session.request_persist(std::move(done));
    };
    callbacks.on_desktop_window_close =
        [this](ae::Obj& application_root, ae::Obj& closed_window,
               apptraverse::ModelObjectProxy& proxy) {
          auto& catalog =
              static_cast<apptraverse::WindowCatalog&>(application_root);
          if (catalog.main_window.id() == closed_window.obj_id) {
            session.runtime.RequestStop();
            return;
          }
          proxy.Invoke(catalog.obj_id,
                       &apptraverse::WindowCatalog::UserCloseWindow,
                       closed_window.obj_id);
        };
    host = std::make_unique<apptraverse::ios::IosApplicationHost>(
        session.runtime, std::move(callbacks));
    host->Start();
  }
};

IosWindowCatalogDriver* Driver() {
  static IosWindowCatalogDriver* driver = new IosWindowCatalogDriver();
  return driver;
}

}  // namespace

@interface IosWindowCatalogAppDelegate : UIResponder <UIApplicationDelegate>
@end

@implementation IosWindowCatalogAppDelegate

- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
  (void)application;
  (void)launchOptions;
  Driver()->Start();
  return YES;
}

- (void)applicationDidEnterBackground:(UIApplication*)application {
  (void)application;
}

- (void)applicationWillEnterForeground:(UIApplication*)application {
  (void)application;
}

@end

int main(int argc, char* argv[]) {
  @autoreleasepool {
    apptraverse::EnableNoninteractiveCrt();
    return UIApplicationMain(
        argc, argv, nil, NSStringFromClass([IosWindowCatalogAppDelegate class]));
  }
}
