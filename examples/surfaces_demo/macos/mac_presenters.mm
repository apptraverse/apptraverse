#import <AppKit/AppKit.h>

#include <cstdio>
#include <cstdlib>

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_macros.h"

#include "mac_app.h"
#include "mac_presenters.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(MacSurfacePresenter);

[[noreturn]] void FatalMac(char const* operation) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), "FatalMac: %s\n", operation);
  WriteFatalStderr(buf);
  std::abort();
}

// Primary-screen conversion: common top-left outer frame ↔ AppKit frame.
// Multi-monitor / non-primary screens are out of scope for this slice.
NSRect CommonBoundsToAppKitFrame(std::int32_t x, std::int32_t y,
                                 std::int32_t width, std::int32_t height) {
  NSScreen* screen = [NSScreen mainScreen];
  if (screen == nil) {
    FatalMac("NSScreen mainScreen");
  }
  NSRect const screen_frame = [screen frame];
  CGFloat const cocoa_y =
      screen_frame.origin.y + screen_frame.size.height -
      static_cast<CGFloat>(y) - static_cast<CGFloat>(height);
  return NSMakeRect(static_cast<CGFloat>(x), cocoa_y,
                    static_cast<CGFloat>(width),
                    static_cast<CGFloat>(height));
}

void AppKitFrameToCommonBounds(NSRect frame, std::int32_t* x, std::int32_t* y,
                               std::int32_t* width, std::int32_t* height) {
  NSScreen* screen = [NSScreen mainScreen];
  if (screen == nil) {
    FatalMac("NSScreen mainScreen");
  }
  NSRect const screen_frame = [screen frame];
  CGFloat const common_y =
      screen_frame.origin.y + screen_frame.size.height -
      (frame.origin.y + frame.size.height);
  *x = static_cast<std::int32_t>(frame.origin.x);
  *y = static_cast<std::int32_t>(common_y);
  *width = static_cast<std::int32_t>(frame.size.width);
  *height = static_cast<std::int32_t>(frame.size.height);
}

}  // namespace
}  // namespace apptraverse

@interface SurfaceWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) apptraverse::MacSurfacePresenter* presenter;
- (void)onAdd:(id)sender;
- (void)onCloseThis:(id)sender;
@end

@implementation SurfaceWindowDelegate
- (void)onAdd:(id)sender {
  (void)sender;
  self.presenter->AddClick();
}

- (void)onCloseThis:(id)sender {
  (void)sender;
  // Last Close this window: whole-app stop without Remove (Surface persists).
  if (self.presenter->surface->surfaces->surfaces.size() == 1) {
    apptraverse::MacRequestApplicationStop(self.presenter->presentation_host);
  } else {
    self.presenter->RemoveClick();
  }
}

- (BOOL)windowShouldClose:(NSWindow*)sender {
  (void)sender;
  // Native red X: always whole-application stop. Never RemoveSurface.
  apptraverse::MacRequestApplicationStop(self.presenter->presentation_host);
  return NO;
}
@end

namespace apptraverse {

void EnsureMacSurfacePresenterRegistration() {
  EnsureDesktopSurfacePresenterRegistration();
  (void)&g_apptraverse_registrar_MacSurfacePresenter;
}

void MacSurfacePresenter::OnLoad() {
  char title[64];
  std::snprintf(title, sizeof(title), "Surface %u", surface->number);

  NSRect const frame = CommonBoundsToAppKitFrame(
      surface->desktop_x, surface->desktop_y, surface->desktop_width,
      surface->desktop_height);

  NSWindow* window = [[NSWindow alloc]
      initWithContentRect:frame
                styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                           NSWindowStyleMaskMiniaturizable |
                           NSWindowStyleMaskResizable)
                  backing:NSBackingStoreBuffered
                    defer:NO];
  if (window == nil) {
    FatalMac("NSWindow initWithContentRect");
  }
  // Place exactly at persisted/common bounds (not cascade/center).
  [window setFrame:frame display:NO];
  [window setTitle:[NSString stringWithUTF8String:title]];
  [window setReleasedWhenClosed:NO];

  SurfaceWindowDelegate* delegate = [[SurfaceWindowDelegate alloc] init];
  if (delegate == nil) {
    FatalMac("SurfaceWindowDelegate alloc");
  }
  delegate.presenter = this;
  [window setDelegate:delegate];

  NSView* content = [window contentView];
  // ContentView origin is bottom-left; place controls near the top edge.
  CGFloat const top_y = NSHeight([content bounds]) - 40.0;
  NSButton* add = [NSButton buttonWithTitle:@"Add"
                                    target:delegate
                                    action:@selector(onAdd:)];
  if (add == nil) {
    FatalMac("NSButton Add");
  }
  [add setFrame:NSMakeRect(12, top_y, 80, 28)];
  [add setAutoresizingMask:NSViewMinYMargin];
  [content addSubview:add];

  NSButton* close_btn =
      [NSButton buttonWithTitle:@"Close this window"
                         target:delegate
                         action:@selector(onCloseThis:)];
  if (close_btn == nil) {
    FatalMac("NSButton Close this window");
  }
  [close_btn setFrame:NSMakeRect(100, top_y, 160, 28)];
  [close_btn setAutoresizingMask:NSViewMinYMargin];
  [content addSubview:close_btn];

  this->window = (__bridge_retained void*)window;
  this->window_delegate = (__bridge_retained void*)delegate;
  this->add_button = (__bridge_retained void*)add;
  this->close_button = (__bridge_retained void*)close_btn;

  [window makeKeyAndOrderFront:nil];
}

void MacSurfacePresenter::OnModelChanged() {}

void MacSurfacePresenter::OnUnload() {
  NSButton* add = (__bridge_transfer NSButton*)add_button;
  add_button = nullptr;
  [add removeFromSuperview];
  (void)add;

  NSButton* close_btn = (__bridge_transfer NSButton*)close_button;
  close_button = nullptr;
  [close_btn removeFromSuperview];
  (void)close_btn;

  NSWindow* window = (__bridge_transfer NSWindow*)this->window;
  this->window = nullptr;
  [window setDelegate:nil];
  // Keep releasedWhenClosed NO: we own the window via bridge_transfer and
  // release it here. orderOut hides it; isVisible filters smoke counts.
  [window setReleasedWhenClosed:NO];
  [window orderOut:nil];
  (void)window;

  SurfaceWindowDelegate* delegate =
      (__bridge_transfer SurfaceWindowDelegate*)window_delegate;
  window_delegate = nullptr;
  (void)delegate;
}

void MacSurfacePresenter::QueueCurrentBounds() {
  NSWindow* window = (__bridge NSWindow*)this->window;
  NSRect const frame = [window frame];
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
  AppKitFrameToCommonBounds(frame, &x, &y, &width, &height);
  UpdateModelBounds(x, y, width, height);
}

}  // namespace apptraverse
