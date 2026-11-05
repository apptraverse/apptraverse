#ifndef APPTRAVERSE_PLATFORM_IOS_IOS_APPLICATION_BRIDGE_H_
#define APPTRAVERSE_PLATFORM_IOS_IOS_APPLICATION_BRIDGE_H_

#import "apptraverse/platform/ios/ATIosApplicationBridge.h"

#include <functional>

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/model_object_proxy.h"

namespace apptraverse::ui::ios {

struct IosApplicationBridgeCallbacks {
  std::function<int(ApplicationModelRuntime&, ApplicationPublishedCallback)>
      run_model;
  std::function<bool(ae::Obj&, ae::Obj&, ModelObjectProxy&,
                     std::function<void()>)>
      close_desktop_window;
  std::function<int(ae::Domain& domain)> persist_model;
};

ATIosApplicationBridge* MakeIosApplicationBridge(
    IosApplicationBridgeCallbacks callbacks,
    id<ATIosApplicationBridgeDelegate> delegate);

}  // namespace apptraverse::ui::ios

#endif  // APPTRAVERSE_PLATFORM_IOS_IOS_APPLICATION_BRIDGE_H_
