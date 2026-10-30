#import <UIKit/UIKit.h>

#import "apptraverse/platform/apple/AppleCatalogBridge.h"

#include "apptraverse/platform/ios/ios_application_host.h"

#include <cstdlib>
#include <vector>

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/apple/apple_swift_entry.h"

namespace apptraverse::ios {
namespace {

[[noreturn]] void FatalIos(char const* message) {
  WriteFatalStderr(message);
  std::abort();
}

}  // namespace

IosApplicationHost::IosApplicationHost(ApplicationModelRuntime& runtime,
                                       IosApplicationHostCallbacks callbacks)
    : runtime_{runtime},
      callbacks_{std::move(callbacks)},
      model_proxy_{[this](ModelObjectProxy::ModelWork work) {
        runtime_.Post(std::move(work));
      }} {
  presentation_host_.opaque = this;
  presentation_host_.on_desktop_window_user_close =
      &IosApplicationHost::OnDesktopWindowUserClose;
  presentation_host_.report_desktop_geometry = false;
  presentation_host_.request_persist = callbacks_.request_persist;
}

IosApplicationHost::~IosApplicationHost() {
  if (model_thread_.joinable()) {
    runtime_.RequestStop();
    model_thread_.join();
  }
}

void IosApplicationHost::OnDesktopWindowUserClose(void* opaque,
                                                  ae::Obj* desktop_window) {
  auto* self = static_cast<IosApplicationHost*>(opaque);
  if (!self->ui_session_ || desktop_window == nullptr ||
      !self->callbacks_.on_desktop_window_close) {
    FatalIos("fatal: iOS desktop close has no UI session\n");
  }
  self->callbacks_.on_desktop_window_close(self->ui_session_->root(),
                                           *desktop_window, self->model_proxy_);
}

void IosApplicationHost::Deliver(ApplicationPublicationKind kind) {
  if (kind == ApplicationPublicationKind::Initial) {
    OnInitialPublished();
    return;
  }
  OnIncrementalPublished();
}

void IosApplicationHost::OnInitialPublished() {
  std::vector<std::uint8_t> bytes = runtime_.TakePublishedCopy();
  if (bytes.empty()) {
    FatalIos("fatal: initial catalog publication was empty\n");
  }
  runtime_.NotifyPublicationConsumed();
  ui_session_.emplace(bytes, &presentation_host_, model_proxy_);
  ui::apple::AppleProjectionBumpRevision(presentation_host_.projection);
  delivered_initial_ = true;
  if (callbacks_.after_initial) {
    callbacks_.after_initial(ui_session_->root());
  }
}

void IosApplicationHost::OnIncrementalPublished() {
  if (!ui_session_) {
    FatalIos("fatal: incremental publication before initial session\n");
  }
  std::vector<std::uint8_t> bytes =
      runtime_.TakePublishedCopy(callbacks_.take_incremental_sidecar);
  if (bytes.empty()) {
    FatalIos("fatal: incremental catalog publication was empty\n");
  }
  runtime_.NotifyPublicationConsumed();
  ui_session_->ApplyStructural(bytes);
  ui::apple::AppleProjectionBumpRevision(presentation_host_.projection);
  if (callbacks_.after_incremental) {
    callbacks_.after_incremental(ui_session_->root());
  }
}

void IosApplicationHost::Start() {
  if (started_) {
    return;
  }
  started_ = true;

  AppleCatalogProjection* projection = [[AppleCatalogProjection alloc] init];
  presentation_host_.projection = (__bridge_retained void*)projection;
  ui::apple::AppleProjectionSetSessionLabel(presentation_host_.projection, "1");
  ui::apple::AppleBindPresentationHost(&presentation_host_);
  presentation_host_.swift_model =
      ApptraverseMakeCatalogModel(presentation_host_.projection);
  if (presentation_host_.swift_model == nullptr) {
    FatalIos("fatal: SwiftUI catalog model was not created\n");
  }
  void* controller_ref =
      ApptraverseIosMakeRootController(presentation_host_.swift_model);
  if (controller_ref == nullptr) {
    FatalIos("fatal: UIHostingController was not created\n");
  }
  root_controller_ = controller_ref;
  UIViewController* controller = (__bridge UIViewController*)controller_ref;
  UIWindow* window =
      [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
  if (window == nil) {
    FatalIos("fatal: UIWindow was not created\n");
  }
  window.rootViewController = controller;
  ui_window_ = (__bridge_retained void*)window;
  [window makeKeyAndVisible];

  model_thread_ = std::thread([this] {
    callbacks_.run_model([this](ApplicationPublicationKind kind) {
      dispatch_async(dispatch_get_main_queue(), ^{
        Deliver(kind);
      });
    });
  });
}

}  // namespace apptraverse::ios
