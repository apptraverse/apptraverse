#ifndef APPTRAVERSE_PLATFORM_MACOS_MACOS_APPLICATION_BRIDGE_H_
#define APPTRAVERSE_PLATFORM_MACOS_MACOS_APPLICATION_BRIDGE_H_

#import "apptraverse/platform/macos/ATMacApplicationBridge.h"

#include <functional>

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/model_object_proxy.h"

namespace apptraverse::ui::macos {

struct MacApplicationBridgeCallbacks {
  std::function<int(ApplicationModelRuntime&, ApplicationPublishedCallback)>
      run_model;
  std::function<bool(ae::Obj&, ae::Obj&, ModelObjectProxy&,
                     std::function<void()>)>
      close_desktop_window;
};

ATMacApplicationBridge* MakeMacApplicationBridge(
    MacApplicationBridgeCallbacks callbacks,
    id<ATMacApplicationBridgeDelegate> delegate);

}  // namespace apptraverse::ui::macos

#endif  // APPTRAVERSE_PLATFORM_MACOS_MACOS_APPLICATION_BRIDGE_H_
