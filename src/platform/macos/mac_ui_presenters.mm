#import <AppKit/AppKit.h>

#include "apptraverse/platform/macos/mac_ui_presenters.h"

#include <cmath>
#include <cstdlib>

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/apple/apple_presentation_host.h"
#include "apptraverse/platform/apple/apple_swift_entry.h"
#include "apptraverse/ui/ui_desktop_window.h"

namespace apptraverse::ui::macos {
namespace {

[[noreturn]] void FatalMac(char const* message) {
  WriteFatalStderr(message);
  std::abort();
}

NSRect ContentRectForModelFrame(std::int32_t x, std::int32_t y,
                                std::int32_t width, std::int32_t height,
                                NSScreen* screen) {
  CGFloat const screen_height = screen != nil ? screen.frame.size.height : 0;
  return NSMakeRect(x, screen_height - static_cast<CGFloat>(y + height), width,
                    height);
}

struct ModelFrame {
  std::int32_t x;
  std::int32_t y;
  std::int32_t width;
  std::int32_t height;
};

ModelFrame ModelFrameFromWindow(NSWindow* window) {
  NSScreen* screen = window.screen != nil ? window.screen : NSScreen.mainScreen;
  NSRect const content = [window contentRectForFrameRect:window.frame];
  CGFloat const screen_height = screen != nil ? screen.frame.size.height : 0;
  ModelFrame frame;
  frame.x = static_cast<std::int32_t>(std::llround(content.origin.x));
  frame.y = static_cast<std::int32_t>(std::llround(
      screen_height - (content.origin.y + content.size.height)));
  frame.width = static_cast<std::int32_t>(std::llround(content.size.width));
  frame.height = static_cast<std::int32_t>(std::llround(content.size.height));
  return frame;
}

}  // namespace

}  // namespace apptraverse::ui::macos

@interface MacCatalogWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) apptraverse::ui::macos::MacDesktopWindowPresenter* presenter;
@end

@implementation MacCatalogWindowDelegate

- (BOOL)windowShouldClose:(NSWindow*)sender {
  (void)sender;
  if (self.presenter != nullptr) {
    self.presenter->NotifyUserClose();
  }
  return NO;
}

- (void)windowDidMove:(NSNotification*)notification {
  (void)notification;
  if (self.presenter != nullptr) {
    self.presenter->ReportNativeFrame();
  }
}

- (void)windowDidEndLiveResize:(NSNotification*)notification {
  (void)notification;
  if (self.presenter != nullptr) {
    self.presenter->ReportNativeFrame();
  }
}

@end

namespace apptraverse::ui::macos {

void MacDesktopWindowPresenter::OnLoad() {
  ui::apple::AppleSyncDesktopPresenter(*this);
  auto desktop = desktop_window();
  if (!desktop) {
    FatalMac("fatal: macOS desktop window missing\n");
  }
  auto* host = ui::apple::AppleRequireHost(presentation_host);
  if (host->swift_model == nullptr) {
    FatalMac("fatal: SwiftUI catalog model missing\n");
  }
  NSScreen* screen = NSScreen.mainScreen;
  NSRect const content = ContentRectForModelFrame(
      desktop->x(), desktop->y(), desktop->client_width(),
      desktop->client_height(), screen);
  NSWindow* window = [[NSWindow alloc]
      initWithContentRect:content
                styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                           NSWindowStyleMaskMiniaturizable |
                           NSWindowStyleMaskResizable)
                  backing:NSBackingStoreBuffered
                    defer:NO];
  if (window == nil) {
    FatalMac("fatal: NSWindow was not created\n");
  }
  window.releasedWhenClosed = NO;
  if (title && locale) {
    if (!title.is_loaded()) {
      title.Load();
    }
    if (!locale.is_loaded()) {
      locale.Load();
    }
    std::string const text = title->Resolve(locale->language_index());
    window.title = [NSString stringWithUTF8String:text.c_str()];
  }
  std::uint64_t const window_id =
      static_cast<std::uint64_t>(desktop->obj_id.id());
  window.accessibilityIdentifier =
      [NSString stringWithFormat:@"catalog.window.%llu",
                                 static_cast<unsigned long long>(window_id)];
  MacCatalogWindowDelegate* delegate = [[MacCatalogWindowDelegate alloc] init];
  delegate.presenter = this;
  window.delegate = delegate;
  native_window_ = (__bridge_retained void*)window;
  window_delegate_ = (__bridge_retained void*)delegate;
  applying_native_frame_ = true;
  [window setFrame:[window frameRectForContentRect:content] display:NO];
  applying_native_frame_ = false;
  ApptraverseMacInstallCatalogContent((__bridge void*)window, host->swift_model,
                                      window_id);
  [window makeKeyAndOrderFront:nil];
  ui::apple::AppleSyncDesktopPresenter(*this);
}

void MacDesktopWindowPresenter::OnModelChanged() {
  ui::apple::AppleSyncDesktopPresenter(*this);
  if (native_window_ == nullptr) {
    return;
  }
  NSWindow* window = (__bridge NSWindow*)native_window_;
  auto desktop = desktop_window();
  if (!desktop || !title || !locale) {
    return;
  }
  if (!title.is_loaded()) {
    title.Load();
  }
  if (!locale.is_loaded()) {
    locale.Load();
  }
  std::string const text = title->Resolve(locale->language_index());
  window.title = [NSString stringWithUTF8String:text.c_str()];
  ApplyRequestedFrameFromMirror();
}

void MacDesktopWindowPresenter::ApplyRequestedFrameFromMirror() {
  if (native_window_ == nullptr || applying_native_frame_) {
    return;
  }
  auto* host = ui::apple::AppleRequireHost(presentation_host);
  if (!host->report_desktop_geometry) {
    return;
  }
  auto desktop = desktop_window();
  if (!desktop) {
    return;
  }
  NSWindow* window = (__bridge NSWindow*)native_window_;
  ModelFrame const native = ModelFrameFromWindow(window);
  if (native.x == desktop->x() && native.y == desktop->y() &&
      native.width == desktop->client_width() &&
      native.height == desktop->client_height()) {
    return;
  }
  NSScreen* screen = window.screen != nil ? window.screen : NSScreen.mainScreen;
  NSRect const content = ContentRectForModelFrame(
      desktop->x(), desktop->y(), desktop->client_width(),
      desktop->client_height(), screen);
  applying_native_frame_ = true;
  [window setFrame:[window frameRectForContentRect:content] display:YES];
  applying_native_frame_ = false;
}

void MacDesktopWindowPresenter::ReportNativeFrame() {
  if (native_window_ == nullptr || applying_native_frame_) {
    return;
  }
  auto* host = ui::apple::AppleRequireHost(presentation_host);
  if (!host->report_desktop_geometry) {
    return;
  }
  NSWindow* window = (__bridge NSWindow*)native_window_;
  ModelFrame const native = ModelFrameFromWindow(window);
  SubmitFrameFromNative(native.x, native.y, native.width, native.height);
}

void MacDesktopWindowPresenter::NotifyUserClose() {
  ui::apple::AppleCloseDesktopPresenter(
      ui::apple::AppleRequireHost(presentation_host), *this);
}

void MacDesktopWindowPresenter::OnUnload() {
  ui::apple::AppleRemoveDesktopPresenter(*this);
  if (window_delegate_ != nullptr) {
    MacCatalogWindowDelegate* delegate =
        (__bridge MacCatalogWindowDelegate*)window_delegate_;
    delegate.presenter = nullptr;
  }
  if (native_window_ != nullptr) {
    NSWindow* window = (__bridge NSWindow*)native_window_;
    window.delegate = nil;
    applying_native_frame_ = true;
    [window orderOut:nil];
    applying_native_frame_ = false;
    (void)(__bridge_transfer NSWindow*)native_window_;
    native_window_ = nullptr;
  }
  if (window_delegate_ != nullptr) {
    (void)(__bridge_transfer MacCatalogWindowDelegate*)window_delegate_;
    window_delegate_ = nullptr;
  }
}

void MacPushButtonPresenter::OnLoad() { ui::apple::AppleSyncButtonPresenter(*this); }

void MacPushButtonPresenter::OnModelChanged() {
  ui::apple::AppleSyncButtonPresenter(*this);
}

void MacPushButtonPresenter::OnUnload() {
  auto* host = ui::apple::AppleRequireHost(presentation_host);
  auto const found = host->buttons_by_control.find(control_id);
  if (found != host->buttons_by_control.end() && found->second == this) {
    host->buttons_by_control.erase(found);
  }
}

void MacListContainerPresenter::OnLoad() {
  ui::apple::AppleSyncListPresenter(*this);
}

void MacListContainerPresenter::OnModelChanged() {
  ui::apple::AppleSyncListPresenter(*this);
}

void MacListContainerPresenter::OnUnload() {
  auto* host = ui::apple::AppleRequireHost(presentation_host);
  if (host->list == this) {
    host->list = nullptr;
  }
}

}  // namespace apptraverse::ui::macos
