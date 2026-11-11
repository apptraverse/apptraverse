#ifndef APPTRAVERSE_PLATFORM_IOS_IOS_APPLICATION_BRIDGE_H_
#define APPTRAVERSE_PLATFORM_IOS_IOS_APPLICATION_BRIDGE_H_

#import "apptraverse/platform/ios/ATIosApplicationBridge.h"

#include <functional>
#include <vector>

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/application_ui_session.h"

namespace apptraverse::ui::ios {

struct IosMobilePresentation {
  std::vector<ae::ObjId> windows;
  ae::ObjId active_window;
};

struct IosApplicationBridgeCallbacks {
  ApplicationModelRuntime* runtime = nullptr;
  UiPresentersPrepareHook prepare_ui_presenters;
  TakePublishedSidecarCallback take_incremental_sidecar;
  std::function<void(ae::Obj&)> after_initial;
  std::function<void(ae::Obj&)> after_incremental;
  std::function<IosMobilePresentation(ae::Obj&)> mobile_presentation;
  std::function<void(ae::Obj&, ModelObjectProxy&, std::int32_t, std::int32_t)>
      on_viewport;
  std::function<void(ae::Obj&, ModelObjectProxy&, int)> on_mobile_swipe;
  std::function<void(ae::Obj&, ModelObjectProxy&, ae::ObjId)> on_mobile_page;
  std::function<int(ApplicationModelRuntime&, ApplicationPublishedCallback)>
      run_model;
  std::function<bool(ae::Obj&, ae::Obj&, ModelObjectProxy&,
                     std::function<void()>)>
      close_desktop_window;
  // Required by requestBackgroundSave; runs on the continuing model session's
  // thread/Domain behind already accepted work. It must preserve the live graph.
  std::function<int(ae::Domain& domain)> persist_model;
};

ATIosApplicationBridge* MakeIosApplicationBridge(
    IosApplicationBridgeCallbacks callbacks,
    id<ATIosApplicationBridgeDelegate> delegate);

}  // namespace apptraverse::ui::ios

#endif  // APPTRAVERSE_PLATFORM_IOS_IOS_APPLICATION_BRIDGE_H_
