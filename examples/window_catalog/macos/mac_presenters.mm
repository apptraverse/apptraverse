#import <AppKit/AppKit.h>

#include <cstdio>
#include <cstdlib>

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_macros.h"

#include "mac_app.h"
#include "mac_presenters.h"
#include "mac_surface_content.h"

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

@interface SurfaceWindowDelegate : NSObject <NSWindowDelegate, MacSurfaceActions>
@property(nonatomic, assign) apptraverse::MacSurfacePresenter* presenter;
@end

@implementation SurfaceWindowDelegate
- (void)addSurface {
  self.presenter->AddClick();
}

- (void)closeThisWindow {
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

- (void)windowDidBecomeKey:(NSNotification*)notification {
  (void)notification;
  // Desktop current Surface: persist via mobile_current for z-order restore.
  self.presenter->PageShown();
}

- (void)windowDidResize:(NSNotification*)notification {
  NSWindow* window = notification.object;
  NSView* content = [window contentView];
  NSSize const size = [content bounds].size;
  // Report only. HStack/VStack switches after model publication / OnModelChanged.
  self.presenter->PresentationSizeChanged(
      static_cast<std::int32_t>(size.width),
      static_cast<std::int32_t>(size.height));
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

  // SwiftUI owns the window content; AppKit keeps the window itself so
  // persisted desktop_* bounds and mobile_current z-order stay enforceable.
  // Initial orientation from the GUI-mirror presentation size (desktop_* seed).
  ApptraverseInstallMacSurfaceContent(window, delegate,
                                      IsWide() ? YES : NO);

  this->window = (__bridge_retained void*)window;
  this->window_delegate = (__bridge_retained void*)delegate;

  // Activates this Surface (windowDidBecomeKey → PageShown). After a full
  // InitializePresenters pass, MacApp::RestoreActiveSurfaceZOrder raises the
  // persisted mobile_current above creation order.
  [window makeKeyAndOrderFront:nil];

  // First real contentView size after SwiftUI install (not desktop_* outer).
  NSView* content = [window contentView];
  NSSize const size = [content bounds].size;
  if (size.width > 0 && size.height > 0) {
    PresentationSizeChanged(static_cast<std::int32_t>(size.width),
                            static_cast<std::int32_t>(size.height));
  }
}

void MacSurfacePresenter::OnModelChanged() {
  NSWindow* window = (__bridge NSWindow*)this->window;
  ApptraverseUpdateMacSurfaceContent(window, IsWide() ? YES : NO);
}

void MacSurfacePresenter::OnUnload() {
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
