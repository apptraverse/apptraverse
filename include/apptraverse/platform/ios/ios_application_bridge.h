#ifndef APPTRAVERSE_PLATFORM_IOS_IOS_APPLICATION_BRIDGE_H_
#define APPTRAVERSE_PLATFORM_IOS_IOS_APPLICATION_BRIDGE_H_

#import "apptraverse/platform/ios/ATIosApplicationBridge.h"

#include <functional>
#include <vector>

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/application_ui_session.h"
#include "apptraverse/ui/ui_device_state.h"

namespace apptraverse::ui::ios {

struct IosMobilePresentation {
  std::vector<ae::ObjId> windows;
  ae::ObjId active_window;
};

struct IosApplicationBridgeCallbacks {
  // Optional application composition; generic hosts do not invent settings.
  std::function<ATIosSettingsState*(ae::Obj&)> settings_state;
  std::function<void(ae::Obj&, ModelObjectProxy&, unsigned, unsigned)>
      on_menu_command;
  std::function<void(ae::Obj&, ModelObjectProxy&, bool)> on_settings_presented;
  ApplicationModelRuntime* runtime = nullptr;
  UiPresentersPrepareHook prepare_ui_presenters;
  TakePublishedSidecarCallback take_incremental_sidecar;
  std::function<void(ae::Obj&)> after_initial;
  std::function<void(ae::Obj&)> after_incremental;
  std::function<IosMobilePresentation(ae::Obj&)> mobile_presentation;
  // Optional for consumers whose model presents a single page rather than a
  // navigation ring (for example MinimalApp's current main/detail window).
  std::function<void(ae::Obj&, ModelObjectProxy&, int)> on_mobile_swipe;
  std::function<void(ae::Obj&, ModelObjectProxy&, ae::ObjId)> on_mobile_page;
  std::function<int(ApplicationModelRuntime&, ApplicationPublishedCallback,
                    UiDeviceEnvironment const&)>
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
