#import <AppKit/AppKit.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>

#include "aether-objects/obj/registry.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"

#include "mac_app.h"
#include "mac_presenters.h"
#include "surfaces_ids.h"
#include "surfaces_lifecycle.h"
#include "surfaces_model.h"

@interface SurfaceWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) apptraverse::MacSurfacePresenter* presenter;
@end

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"     \
                << __LINE__ << '\n';                                        \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

void RunOnMain(void (^block)(void)) {
  if ([NSThread isMainThread]) {
    block();
    return;
  }
  // Async + semaphore so other main-queue work (publication / NSApp stop)
  // can run between driver waits — unlike dispatch_sync which starves it.
  dispatch_semaphore_t const done = dispatch_semaphore_create(0);
  dispatch_async(dispatch_get_main_queue(), ^{
    block();
    dispatch_semaphore_signal(done);
  });
  dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
}

NSArray<NSWindow*>* SurfaceWindows() {
  NSMutableArray<NSWindow*>* found = [NSMutableArray array];
  for (NSWindow* window in [NSApp windows]) {
    if (![window isVisible]) {
      continue;
    }
    if ([[window title] hasPrefix:@"Surface "]) {
      [found addObject:window];
    }
  }
  return found;
}

NSWindow* FindSurfaceWindow(std::uint32_t number) {
  NSString* want = [NSString stringWithFormat:@"Surface %u", number];
  for (NSWindow* window in SurfaceWindows()) {
    if ([[window title] isEqualToString:want]) {
      return window;
    }
  }
  return nil;
}

NSButton* FindButton(NSWindow* window, NSString* title) {
  for (NSView* view in [[window contentView] subviews]) {
    if (![view isKindOfClass:[NSButton class]]) {
      continue;
    }
    auto* button = static_cast<NSButton*>(view);
    if ([[button title] isEqualToString:title]) {
      return button;
    }
  }
  return nil;
}

bool WaitSurfaceCount(int expected, std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    __block NSUInteger count = 0;
    RunOnMain(^{ count = [SurfaceWindows() count]; });
    if (static_cast<int>(count) == expected) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
  }
  return false;
}

bool WaitSurface(std::uint32_t number, NSWindow* __strong* out,
                 std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    __block NSWindow* found = nil;
    RunOnMain(^{ found = FindSurfaceWindow(number); });
    if (found != nil) {
      if (out != nullptr) {
        *out = found;
      }
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
  }
  return false;
}

void ClickButton(NSWindow* window, NSString* title) {
  RunOnMain(^{
    NSButton* button = FindButton(window, title);
    CHECK(button != nil);
    [button performClick:nil];
  });
}

void PlaceWindow(NSWindow* window, std::int32_t x, std::int32_t y,
                 std::int32_t w, std::int32_t h) {
  RunOnMain(^{
    NSScreen* screen = [NSScreen mainScreen];
    CHECK(screen != nil);
    NSRect const screen_frame = [screen frame];
    CGFloat const cocoa_y = screen_frame.origin.y + screen_frame.size.height -
                            static_cast<CGFloat>(y) -
                            static_cast<CGFloat>(h);
    [window setFrame:NSMakeRect(static_cast<CGFloat>(x), cocoa_y,
                                static_cast<CGFloat>(w),
                                static_cast<CGFloat>(h))
             display:YES];
  });
  std::this_thread::sleep_for(std::chrono::milliseconds{50});
}

NSRect WindowFrame(NSWindow* window) {
  __block NSRect frame{};
  RunOnMain(^{ frame = [window frame]; });
  return frame;
}

bool FrameNear(NSRect a, NSRect b, CGFloat tol) {
  return std::fabs(a.origin.x - b.origin.x) <= tol &&
         std::fabs(a.origin.y - b.origin.y) <= tol &&
         std::fabs(a.size.width - b.size.width) <= tol &&
         std::fabs(a.size.height - b.size.height) <= tol;
}

void RequestNativeClose(NSWindow* window) {
  RunOnMain(^{ [window performClose:nil]; });
}

bool WaitIsKeyWindow(NSWindow* window, std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    __block BOOL is_key = NO;
    RunOnMain(^{ is_key = [window isKeyWindow]; });
    if (is_key) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
  }
  return false;
}

MacSurfacePresenter* PresenterForWindow(NSWindow* window) {
  __block MacSurfacePresenter* presenter = nullptr;
  RunOnMain(^{
    id delegate = [window delegate];
    CHECK(delegate != nil);
    CHECK([delegate isKindOfClass:[SurfaceWindowDelegate class]]);
    presenter = static_cast<SurfaceWindowDelegate*>(delegate).presenter;
  });
  return presenter;
}

void TestPresenterHierarchy() {
  EnsureObjectRegistration();
  EnsureSurfacesModelRegistration();
  EnsureMacSurfacePresenterRegistration();
  auto& registry = ae::Registry::GetRegistry();
  CHECK(registry.GenerationDistance(SurfacePresenter::kClassId,
                                    DesktopSurfacePresenter::kClassId) == 1);
  CHECK(registry.GenerationDistance(DesktopSurfacePresenter::kClassId,
                                    MacSurfacePresenter::kClassId) == 1);
  CHECK(registry.GenerationDistance(SurfacePresenter::kClassId,
                                    MacSurfacePresenter::kClassId) == 2);
}

void TestCloseButtonRemovesOne() {
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_macos_close_btn";
  std::filesystem::remove_all(dir);

  EnsureObjectRegistration();
  EnsureSurfacesModelRegistration();
  EnsureMacSurfacePresenterRegistration();

  NSRect r1{};
  NSRect r3{};
  NSRect r4{};

  {
    MacApp app;
    std::atomic<bool> started{false};
    std::atomic<bool> finished{false};
    std::thread driver{[&] {
      while (!started.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
      }
      NSWindow* s1 = nil;
      CHECK(WaitSurface(1, &s1, std::chrono::seconds{60}));
      auto* p1 = PresenterForWindow(s1);
      CHECK(p1->GetClassId() == MacSurfacePresenter::kClassId);

      ClickButton(s1, @"Add");
      NSWindow* s2 = nil;
      CHECK(WaitSurface(2, &s2, std::chrono::seconds{30}));
      ClickButton(s2, @"Add");
      NSWindow* s3 = nil;
      CHECK(WaitSurface(3, &s3, std::chrono::seconds{30}));
      CHECK(WaitSurfaceCount(3, std::chrono::seconds{10}));

      void* const s1_id = (__bridge void*)s1;
      void* const s3_id = (__bridge void*)s3;

      ClickButton(s2, @"Close this window");
      CHECK(WaitSurfaceCount(2, std::chrono::seconds{30}));
      CHECK(WaitSurface(1, &s1, std::chrono::seconds{5}));
      CHECK(WaitSurface(3, &s3, std::chrono::seconds{5}));
      CHECK((__bridge void*)s1 == s1_id);
      CHECK((__bridge void*)s3 == s3_id);

      ClickButton(s3, @"Add");
      NSWindow* s4 = nil;
      CHECK(WaitSurface(4, &s4, std::chrono::seconds{30}));
      CHECK(WaitSurfaceCount(3, std::chrono::seconds{10}));

      PlaceWindow(s1, 60, 70, 380, 250);
      PlaceWindow(s3, 160, 170, 400, 260);
      PlaceWindow(s4, 260, 270, 420, 270);
      r1 = WindowFrame(s1);
      r3 = WindowFrame(s3);
      r4 = WindowFrame(s4);

      RequestNativeClose(s3);
      // Do not RunOnMain after stop: main leaves [NSApp run] and joins us.
      while (!finished.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
      }
    }};
    started.store(true);
    CHECK(app.Run(dir) == 0);
    finished.store(true);
    driver.join();
  }

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{surfaces_demo::ToObjId(
                  surfaces_demo::ObjId::Application)});
  CHECK(application->surfaces->surfaces.size() == 3);
  CHECK(application->surfaces->surfaces[0]->number == 1);
  CHECK(application->surfaces->surfaces[1]->number == 3);
  CHECK(application->surfaces->surfaces[2]->number == 4);

  {
    MacApp app2;
    std::atomic<bool> started{false};
    std::atomic<bool> finished{false};
    std::thread driver{[&] {
      while (!started.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
      }
      NSWindow* rs1 = nil;
      NSWindow* rs3 = nil;
      NSWindow* rs4 = nil;
      CHECK(WaitSurface(1, &rs1, std::chrono::seconds{60}));
      CHECK(WaitSurface(3, &rs3, std::chrono::seconds{30}));
      CHECK(WaitSurface(4, &rs4, std::chrono::seconds{30}));
      CHECK(WaitSurfaceCount(3, std::chrono::seconds{10}));
      CHECK(FindSurfaceWindow(2) == nil);
      CHECK(FrameNear(WindowFrame(rs1), r1, 2.0));
      CHECK(FrameNear(WindowFrame(rs3), r3, 2.0));
      CHECK(FrameNear(WindowFrame(rs4), r4, 2.0));
      RequestNativeClose(rs1);
      while (!finished.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
      }
    }};
    started.store(true);
    CHECK(app2.Run(dir) == 0);
    finished.store(true);
    driver.join();
  }
  std::filesystem::remove_all(dir);
}

void TestNativeXKeepsAllSurfaces() {
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_macos_native_x";
  std::filesystem::remove_all(dir);

  EnsureObjectRegistration();
  EnsureSurfacesModelRegistration();
  EnsureMacSurfacePresenterRegistration();

  NSRect r1{};
  NSRect r2{};
  NSRect r3{};

  {
    MacApp app;
    std::atomic<bool> started{false};
    std::atomic<bool> finished{false};
    std::thread driver{[&] {
      while (!started.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
      }
      NSWindow* s1 = nil;
      CHECK(WaitSurface(1, &s1, std::chrono::seconds{60}));
      ClickButton(s1, @"Add");
      NSWindow* s2 = nil;
      CHECK(WaitSurface(2, &s2, std::chrono::seconds{30}));
      ClickButton(s2, @"Add");
      NSWindow* s3 = nil;
      CHECK(WaitSurface(3, &s3, std::chrono::seconds{30}));
      CHECK(WaitSurfaceCount(3, std::chrono::seconds{10}));

      PlaceWindow(s1, 50, 60, 370, 240);
      PlaceWindow(s2, 150, 160, 390, 250);
      PlaceWindow(s3, 250, 260, 410, 260);
      r1 = WindowFrame(s1);
      r2 = WindowFrame(s2);
      r3 = WindowFrame(s3);

      RequestNativeClose(s2);
      while (!finished.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
      }
    }};
    started.store(true);
    CHECK(app.Run(dir) == 0);
    finished.store(true);
    driver.join();
  }

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{surfaces_demo::ToObjId(
                  surfaces_demo::ObjId::Application)});
  CHECK(application->surfaces->surfaces.size() == 3);
  CHECK(application->surfaces->surfaces[0]->number == 1);
  CHECK(application->surfaces->surfaces[1]->number == 2);
  CHECK(application->surfaces->surfaces[2]->number == 3);

  {
    MacApp app2;
    std::atomic<bool> started{false};
    std::atomic<bool> finished{false};
    std::thread driver{[&] {
      while (!started.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
      }
      NSWindow* rs1 = nil;
      NSWindow* rs2 = nil;
      NSWindow* rs3 = nil;
      CHECK(WaitSurface(1, &rs1, std::chrono::seconds{60}));
      CHECK(WaitSurface(2, &rs2, std::chrono::seconds{30}));
      CHECK(WaitSurface(3, &rs3, std::chrono::seconds{30}));
      CHECK(WaitSurfaceCount(3, std::chrono::seconds{10}));
      CHECK(FrameNear(WindowFrame(rs1), r1, 2.0));
      CHECK(FrameNear(WindowFrame(rs2), r2, 2.0));
      CHECK(FrameNear(WindowFrame(rs3), r3, 2.0));

      ClickButton(rs2, @"Close this window");
      CHECK(WaitSurfaceCount(2, std::chrono::seconds{30}));
      ClickButton(rs1, @"Close this window");
      CHECK(WaitSurfaceCount(1, std::chrono::seconds{30}));
      NSWindow* last = nil;
      CHECK(WaitSurface(3, &last, std::chrono::seconds{5}));
      ClickButton(last, @"Close this window");
      while (!finished.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
      }
    }};
    started.store(true);
    CHECK(app2.Run(dir) == 0);
    finished.store(true);
    driver.join();
  }

  DirectoryDomainStorage storage2{dir};
  ae::Domain domain2{storage2};
  auto application2 = LoadApplication<Application>(
      domain2, ae::ObjId{surfaces_demo::ToObjId(
                   surfaces_demo::ObjId::Application)});
  CHECK(application2->surfaces->surfaces.size() == 1);
  CHECK(application2->surfaces->surfaces[0]->number == 3);

  std::filesystem::remove_all(dir);
}

void TestActiveZOrderRestored() {
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_surfaces_macos_zorder";
  std::filesystem::remove_all(dir);

  EnsureObjectRegistration();
  EnsureSurfacesModelRegistration();
  EnsureMacSurfacePresenterRegistration();

  {
    MacApp app;
    std::atomic<bool> started{false};
    std::atomic<bool> finished{false};
    std::thread driver{[&] {
      while (!started.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
      }
      NSWindow* s1 = nil;
      CHECK(WaitSurface(1, &s1, std::chrono::seconds{60}));
      ClickButton(s1, @"Add");
      NSWindow* s2 = nil;
      CHECK(WaitSurface(2, &s2, std::chrono::seconds{30}));
      ClickButton(s2, @"Add");
      NSWindow* s3 = nil;
      CHECK(WaitSurface(3, &s3, std::chrono::seconds{30}));
      CHECK(WaitSurfaceCount(3, std::chrono::seconds{10}));

      // Activate Surface 2 so mobile_current / z-order restore targets it.
      // makeKeyAndOrderFront → windowDidBecomeKey → PageShown; stop path also
      // QueueKeyWindowAsCurrent before Save (same contract as Win/Linux).
      RunOnMain(^{
        [s2 makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
      });
      CHECK(WaitIsKeyWindow(s2, std::chrono::seconds{5}));

      RequestNativeClose(s2);
      while (!finished.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
      }
    }};
    started.store(true);
    CHECK(app.Run(dir) == 0);
    finished.store(true);
    driver.join();
  }

  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{surfaces_demo::ToObjId(
                  surfaces_demo::ObjId::Application)});
  CHECK(application->surfaces->mobile_current);
  CHECK(application->surfaces->mobile_current->number == 2);

  {
    MacApp app2;
    std::atomic<bool> started{false};
    std::atomic<bool> finished{false};
    std::thread driver{[&] {
      while (!started.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
      }
      NSWindow* rs1 = nil;
      NSWindow* rs2 = nil;
      NSWindow* rs3 = nil;
      CHECK(WaitSurface(1, &rs1, std::chrono::seconds{60}));
      CHECK(WaitSurface(2, &rs2, std::chrono::seconds{30}));
      CHECK(WaitSurface(3, &rs3, std::chrono::seconds{30}));
      CHECK(WaitSurfaceCount(3, std::chrono::seconds{10}));
      // RestoreActiveSurfaceZOrder raises mobile_current after presenters init;
      // poll past creation-order makeKeyAndOrderFront from OnLoad.
      CHECK(WaitIsKeyWindow(rs2, std::chrono::seconds{5}));

      RequestNativeClose(rs2);
      while (!finished.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
      }
    }};
    started.store(true);
    CHECK(app2.Run(dir) == 0);
    finished.store(true);
    driver.join();
  }
  std::filesystem::remove_all(dir);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::test::TestPresenterHierarchy();
  apptraverse::test::TestCloseButtonRemovesOne();
  apptraverse::test::TestNativeXKeepsAllSurfaces();
  apptraverse::test::TestActiveZOrderRestored();
  std::cout << "surfaces_macos_smoke_test OK\n";
  return 0;
}
