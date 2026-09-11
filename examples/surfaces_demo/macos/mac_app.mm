#import <AppKit/AppKit.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_serialization.h"

#include "mac_app.h"
#include "mac_presenters.h"

namespace apptraverse {
namespace {

[[noreturn]] void FatalMac(char const* operation) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "FatalMac: %s\n", operation);
  WriteFatalStderr(buf);
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
}  // namespace apptraverse

@interface SurfacesMacAppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, assign) apptraverse::MacApp* app;
@end

@implementation SurfacesMacAppDelegate
- (NSApplicationTerminateReply)applicationShouldTerminate:
    (NSApplication*)sender {
  (void)sender;
  // Cmd-Q / Quit: same graceful path as red X (snapshot → RequestStop).
  self.app->RequestApplicationStop();
  return NSTerminateCancel;
}
@end

namespace apptraverse {

void MacRequestApplicationStop(void* presentation_host) {
  auto* app = static_cast<MacApp*>(presentation_host);
  app->RequestApplicationStop();
}

void MacApp::MainOpTrampoline(void* raw) {
  auto* ctx = static_cast<MainOpCtx*>(raw);
  MacApp* app = ctx->app;
  MainOp const op = ctx->op;
  delete ctx;
  switch (op) {
    case MainOp::InitialPublished:
      app->OnInitialPublished();
      break;
    case MainOp::IncrementalPublished:
      app->OnIncrementalPublished();
      break;
    case MainOp::ModelFinished:
      app->OnModelFinished();
      break;
  }
}

void MacApp::PostMain(void (*fn)(void*), void* ctx) {
  dispatch_async_f(dispatch_get_main_queue(), ctx, fn);
}

void MacApp::QueueAllWindowBounds() {
  for (auto const& surface : ui_application_->surfaces->surfaces) {
    MacSurfacePresenter::ptr presenter{surface->presenter};
    presenter->QueueCurrentBounds();
  }
}

void MacApp::QueueKeyWindowAsCurrent() {
  NSWindow* const key = [NSApp keyWindow];
  if (key == nil) {
    return;
  }
  for (auto const& surface : ui_application_->surfaces->surfaces) {
    MacSurfacePresenter::ptr presenter{surface->presenter};
    NSWindow* window = (__bridge NSWindow*)presenter->window;
    if (window == key) {
      presenter->PageShown();
      return;
    }
  }
}

void MacApp::RestoreActiveSurfaceZOrder() {
  auto& surfaces = ui_application_->surfaces->surfaces;
  Surface::ptr target = ui_application_->surfaces->mobile_current;
  if (!target) {
    if (surfaces.empty()) {
      return;
    }
    // Pre-z-order state: keep last-created (creation order) on top.
    target = surfaces.back();
  }
  MacSurfacePresenter::ptr presenter{target->presenter};
  NSWindow* window = (__bridge NSWindow*)presenter->window;
  [window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
  // Explicit PageShown: windowDidBecomeKey may not fire when already key.
  presenter->PageShown();
}

void MacApp::RequestApplicationStop() {
  if (stop_requested_) {
    return;
  }
  stop_requested_ = true;
  // Current Surface + geometry must be accepted before stop so Save persists
  // both mobile_current (z-order) and desktop_*.
  if (ui_application_) {
    QueueKeyWindowAsCurrent();
    QueueAllWindowBounds();
  }
  session_.RequestStop();
}

void MacApp::OnInitialPublished() {
  std::vector<std::uint8_t> bytes;
  {
    std::lock_guard<std::mutex> lock{session_.mu};
    bytes = session_.channel.TakePublishedCopy();
  }
  session_.cv.notify_all();
  ui_domain_ = std::make_unique<ae::Domain>(ui_storage_);
  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  auto ui_root = LoadInitialPublication(in, *ui_domain_, ui_storage_);
  ui_application_ = Application::ptr::MakeFromThis(
      static_cast<Application*>(ui_root.get()));
  InitializePresenters(*ui_application_, this, &*model_proxy_);
  RestoreActiveSurfaceZOrder();

  NSWindow* loading = (__bridge_transfer NSWindow*)loading_window_;
  loading_window_ = nullptr;
  [loading orderOut:nil];
  (void)loading;
}

void MacApp::OnIncrementalPublished() {
  std::vector<std::uint8_t> bytes;
  {
    std::lock_guard<std::mutex> lock{session_.mu};
    bytes = session_.channel.TakePublishedCopy();
  }
  session_.cv.notify_all();
  ApplySurfacesStructural(bytes, *ui_application_, ui_storage_, this,
                          &*model_proxy_);
}

void MacApp::OnModelFinished() {
  model_finished_ = true;
  [NSApp stop:nil];
  WakeNsApp();
}

int MacApp::Run(std::filesystem::path const& state_dir) {
  EnsureMacSurfacePresenterRegistration();

  [NSApplication sharedApplication];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

  SurfacesMacAppDelegate* delegate = [[SurfacesMacAppDelegate alloc] init];
  if (delegate == nil) {
    FatalMac("SurfacesMacAppDelegate alloc");
  }
  delegate.app = this;
  [NSApp setDelegate:delegate];
  app_delegate_ = (__bridge_retained void*)delegate;

  session_.state_dir = state_dir;
  model_proxy_.emplace([this](ModelObjectProxy::ModelWork work) {
    session_.Post(std::move(work));
  });

  NSWindow* loading = [[NSWindow alloc]
      initWithContentRect:NSMakeRect(200, 200, 280, 120)
                styleMask:(NSWindowStyleMaskTitled)
                  backing:NSBackingStoreBuffered
                    defer:NO];
  if (loading == nil) {
    FatalMac("NSWindow Loading");
  }
  [loading setTitle:@"Loading"];
  [loading setReleasedWhenClosed:NO];
  NSTextField* label =
      [NSTextField labelWithString:@"Loading"];
  [label setFrame:NSMakeRect(20, 40, 240, 24)];
  [label setAlignment:NSTextAlignmentCenter];
  [[loading contentView] addSubview:label];
  [loading makeKeyAndOrderFront:nil];
  loading_window_ = (__bridge_retained void*)loading;

  [NSApp activateIgnoringOtherApps:YES];

  model_thread_ = std::thread([this] {
    session_.Run([this](SurfacesPublicationKind kind) {
      auto* ctx = new MainOpCtx{
          this, kind == SurfacesPublicationKind::Initial
                    ? MainOp::InitialPublished
                    : MainOp::IncrementalPublished};
      PostMain(&MacApp::MainOpTrampoline, ctx);
    });
    auto* ctx = new MainOpCtx{this, MainOp::ModelFinished};
    PostMain(&MacApp::MainOpTrampoline, ctx);
  });

  [NSApp run];

  model_thread_.join();
  if (ui_application_) {
    UnloadPresenters(*ui_application_);
  }
  ui_application_ = {};
  ui_domain_.reset();
  model_proxy_.reset();

  SurfacesMacAppDelegate* app_delegate =
      (__bridge_transfer SurfacesMacAppDelegate*)app_delegate_;
  app_delegate_ = nullptr;
  [NSApp setDelegate:nil];
  (void)app_delegate;

  if (loading_window_ != nullptr) {
    NSWindow* leftover = (__bridge_transfer NSWindow*)loading_window_;
    loading_window_ = nullptr;
    [leftover orderOut:nil];
    (void)leftover;
  }
  return 0;
}

}  // namespace apptraverse
