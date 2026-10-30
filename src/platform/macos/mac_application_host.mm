#import <AppKit/AppKit.h>

#import "apptraverse/platform/apple/AppleCatalogBridge.h"

#include "apptraverse/platform/macos/mac_application_host.h"

#include <cstdlib>
#include <vector>

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/apple/apple_swift_entry.h"
#include "apptraverse/platform/macos/mac_ui_presenters.h"

namespace apptraverse::macos {
namespace {

[[noreturn]] void FatalMac(char const* message) {
  WriteFatalStderr(message);
  std::abort();
}

void WakeNsApp() {
  NSEvent* event =
      [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                         location:NSZeroPoint
                    modifierFlags:0
                        timestamp:0
                     windowNumber:0
                          context:nil
                          subtype:0
                            data1:0
                            data2:0];
  [NSApp postEvent:event atStart:YES];
}

}  // namespace
}  // namespace apptraverse::macos

@interface MacCatalogAppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, assign) apptraverse::macos::MacApplicationHost* host;
@end

@implementation MacCatalogAppDelegate

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
  (void)sender;
  if (self.host != nullptr) {
    self.host->RequestApplicationStop();
  }
  return NSTerminateCancel;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
  (void)sender;
  return NO;
}

@end

namespace apptraverse::macos {

MacApplicationHost::MacApplicationHost(ApplicationModelRuntime& runtime,
                                       MacApplicationHostCallbacks callbacks)
    : runtime_{runtime},
      callbacks_{std::move(callbacks)},
      model_proxy_{[this](ModelObjectProxy::ModelWork work) {
        runtime_.Post(std::move(work));
      }} {
  presentation_host_.opaque = this;
  presentation_host_.on_desktop_window_user_close =
      &MacApplicationHost::OnDesktopWindowUserClose;
  presentation_host_.report_desktop_geometry = true;
  presentation_host_.request_persist = callbacks_.request_persist;
}

MacApplicationHost::~MacApplicationHost() {
  if (model_thread_.joinable()) {
    model_thread_.join();
  }
}

void MacApplicationHost::OnDesktopWindowUserClose(void* opaque,
                                                  ae::Obj* desktop_window) {
  auto* self = static_cast<MacApplicationHost*>(opaque);
  if (!self->ui_session_ || desktop_window == nullptr ||
      !self->callbacks_.on_desktop_window_close) {
    FatalMac("fatal: macOS desktop close has no UI session\n");
  }
  self->callbacks_.on_desktop_window_close(self->ui_session_->root(),
                                           *desktop_window, self->model_proxy_);
}

void MacApplicationHost::Deliver(ApplicationPublicationKind kind) {
  if (kind == ApplicationPublicationKind::Initial) {
    OnInitialPublished();
    return;
  }
  OnIncrementalPublished();
}

void MacApplicationHost::OnInitialPublished() {
  std::vector<std::uint8_t> bytes = runtime_.TakePublishedCopy();
  if (bytes.empty()) {
    FatalMac("fatal: initial catalog publication was empty\n");
  }
  runtime_.NotifyPublicationConsumed();
  ui_session_.emplace(bytes, &presentation_host_, model_proxy_);
  ui::apple::AppleProjectionBumpRevision(presentation_host_.projection);
  delivered_initial_ = true;
  if (callbacks_.after_initial) {
    callbacks_.after_initial(ui_session_->root());
  }
}

void MacApplicationHost::OnIncrementalPublished() {
  if (!ui_session_) {
    FatalMac("fatal: incremental publication before initial session\n");
  }
  std::vector<std::uint8_t> bytes =
      runtime_.TakePublishedCopy(callbacks_.take_incremental_sidecar);
  if (bytes.empty()) {
    FatalMac("fatal: incremental catalog publication was empty\n");
  }
  runtime_.NotifyPublicationConsumed();
  ui_session_->ApplyStructural(bytes);
  ui::apple::AppleProjectionBumpRevision(presentation_host_.projection);
  if (callbacks_.after_incremental) {
    callbacks_.after_incremental(ui_session_->root());
  }
}

void MacApplicationHost::RequestApplicationStop() {
  if (callbacks_.request_stop) {
    callbacks_.request_stop();
  }
}

void MacApplicationHost::FocusWindow(std::uint64_t window_obj_id) {
  auto const found = presentation_host_.windows_by_id.find(window_obj_id);
  if (found == presentation_host_.windows_by_id.end() ||
      found->second == nullptr) {
    return;
  }
  auto* presenter =
      static_cast<ui::macos::MacDesktopWindowPresenter*>(found->second);
  if (presenter->native_window() == nullptr) {
    return;
  }
  NSWindow* window = (__bridge NSWindow*)presenter->native_window();
  [window makeKeyAndOrderFront:nil];
}

void MacApplicationHost::Run() {
  @autoreleasepool {
    NSApplication* app = [NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];
    MacCatalogAppDelegate* delegate = [[MacCatalogAppDelegate alloc] init];
    delegate.host = this;
    app.delegate = delegate;
    app_delegate_ = (__bridge_retained void*)delegate;

    AppleCatalogProjection* projection = [[AppleCatalogProjection alloc] init];
    presentation_host_.projection = (__bridge_retained void*)projection;
    ui::apple::AppleProjectionSetSessionLabel(presentation_host_.projection, "1");
    ui::apple::AppleBindPresentationHost(&presentation_host_);
    presentation_host_.swift_model =
        ApptraverseMakeCatalogModel(presentation_host_.projection);
    if (presentation_host_.swift_model == nullptr) {
      FatalMac("fatal: SwiftUI catalog model was not created\n");
    }

    model_thread_ = std::thread([this] {
      callbacks_.run_model([this](ApplicationPublicationKind kind) {
        dispatch_async(dispatch_get_main_queue(), ^{
          Deliver(kind);
        });
      });
      dispatch_async(dispatch_get_main_queue(), ^{
        [NSApp stop:nil];
        WakeNsApp();
      });
    });

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [app activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
    [app run];

    if (model_thread_.joinable()) {
      model_thread_.join();
    }
    ui_session_.reset();
    ui::apple::AppleUnbindPresentationHost(presentation_host_.projection);
    if (presentation_host_.swift_model != nullptr) {
      ApptraverseReleaseCatalogModel(presentation_host_.swift_model);
      presentation_host_.swift_model = nullptr;
    }
    if (presentation_host_.projection != nullptr) {
      (void)(__bridge_transfer AppleCatalogProjection*)
          presentation_host_.projection;
      presentation_host_.projection = nullptr;
    }
    if (app_delegate_ != nullptr) {
      (void)(__bridge_transfer MacCatalogAppDelegate*)app_delegate_;
      app_delegate_ = nullptr;
    }
  }
}

}  // namespace apptraverse::macos
