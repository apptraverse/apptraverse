#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/chat.h"
#include "apptraverse/chat_presenter.h"
#include "apptraverse/node.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/window_presenter.h"

#include "../examples/single_client_chat/windows/display_environment_changed_event.h"
#include "../examples/single_client_chat/windows/window_bounds_changed_event.h"
#include "../examples/single_client_chat/windows/windows_window.h"

namespace apptraverse::test {

APPTRAVERSE_REGISTER(WindowsWindow);
APPTRAVERSE_REGISTER(WindowBoundsChangedEvent);
APPTRAVERSE_REGISTER(DisplayEnvironmentChangedEvent);

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

WindowsWindow::ptr MakeWindow(ae::Domain& domain, ae::ObjId id) {
  auto window = WindowsWindow::ptr::Create(ae::CreateWith{domain}.with_id(id));
  window->x = 10;
  window->y = 20;
  window->width = 400;
  window->height = 300;
  return window;
}

DisplayEnvironmentChangedEvent::ptr MakeEnv(
    ae::Domain& domain, ae::ObjId id, std::int32_t work_left,
    std::int32_t work_top, std::int32_t work_right, std::int32_t work_bottom,
    std::int32_t candidate_x, std::int32_t candidate_y,
    std::int32_t candidate_width, std::int32_t candidate_height) {
  auto event =
      DisplayEnvironmentChangedEvent::ptr::Create(ae::CreateWith{domain}.with_id(id));
  event->work_left = work_left;
  event->work_top = work_top;
  event->work_right = work_right;
  event->work_bottom = work_bottom;
  event->candidate_x = candidate_x;
  event->candidate_y = candidate_y;
  event->candidate_width = candidate_width;
  event->candidate_height = candidate_height;
  event->dpi = 96;
  return event;
}

void TestA_AlreadyInsideUnchanged() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto window = MakeWindow(domain, 1);
  auto event = MakeEnv(domain, 2, 0, 0, 1920, 1080, 10, 20, 400, 300);
  event->ApplyTo(*window);
  CHECK(window->x == 10);
  CHECK(window->y == 20);
  CHECK(window->width == 400);
  CHECK(window->height == 300);
}

void TestB_PartialOverflowMovesInside() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto window = MakeWindow(domain, 1);
  auto event = MakeEnv(domain, 2, 0, 0, 1000, 800, 900, 700, 200, 150);
  event->ApplyTo(*window);
  CHECK(window->width == 200);
  CHECK(window->height == 150);
  CHECK(window->x == 800);
  CHECK(window->y == 650);
}

void TestC_LargerThanWorkAreaShrinks() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto window = MakeWindow(domain, 1);
  auto event = MakeEnv(domain, 2, 0, 0, 800, 600, -50, -40, 2000, 1500);
  event->ApplyTo(*window);
  CHECK(window->width == 800);
  CHECK(window->height == 600);
  CHECK(window->x == 0);
  CHECK(window->y == 0);
}

void TestD_NegativeWorkAreaKeepsLeftMonitor() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto window = MakeWindow(domain, 1);
  auto event = MakeEnv(domain, 2, -1920, 0, 0, 1080, -1800, 100, 500, 400);
  event->ApplyTo(*window);
  CHECK(window->x == -1800);
  CHECK(window->y == 100);
  CHECK(window->width == 500);
  CHECK(window->height == 400);
}

void TestE_CandidateOutsideStillClamped() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto window = MakeWindow(domain, 1);
  auto event = MakeEnv(domain, 2, 100, 200, 900, 700, 5000, -4000, 300, 250);
  event->ApplyTo(*window);
  CHECK(window->width == 300);
  CHECK(window->height == 250);
  CHECK(window->x == 600);
  CHECK(window->y == 200);
}

void TestWindowBoundsChangedAndNoJournal() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto window = MakeWindow(domain, 1);
  auto bounds = WindowBoundsChangedEvent::ptr::Create(
      ae::CreateWith{domain}.with_id(3));
  bounds->x = 40;
  bounds->y = 50;
  bounds->width = 640;
  bounds->height = 480;
  bounds->ApplyTo(*window);
  CHECK(window->x == 40);
  CHECK(window->y == 50);
  CHECK(window->width == 640);
  CHECK(window->height == 480);
  bool const window_is_not_node = !std::is_base_of_v<Node, WindowsWindow>;
  CHECK(window_is_not_node);
}

// Case F: invalid zero work dimensions trip assert(work_width > 0) /
// assert(work_height > 0) inside WindowsWindow::Apply. That contract is
// enforced at the call site; it is not executed as a regular CHECK here.

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestA_AlreadyInsideUnchanged();
  apptraverse::test::TestB_PartialOverflowMovesInside();
  apptraverse::test::TestC_LargerThanWorkAreaShrinks();
  apptraverse::test::TestD_NegativeWorkAreaKeepsLeftMonitor();
  apptraverse::test::TestE_CandidateOutsideStillClamped();
  apptraverse::test::TestWindowBoundsChangedAndNoJournal();
  std::cout << "windows_window_policy_test OK\n";
  return 0;
}
