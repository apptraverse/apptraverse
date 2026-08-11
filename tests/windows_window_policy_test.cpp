#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "model/chat.h"
#include "model/chat_presenter.h"
#include "model/registration.h"
#include "apptraverse/node.h"
#include "apptraverse/object_macros.h"
#include "model/window_changed_event.h"
#include "model/window_presenter.h"

#include "../examples/single_client_chat/windows/windows_window.h"

namespace apptraverse::test {

APPTRAVERSE_REGISTER(WindowsWindow);

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

static_assert(std::is_base_of_v<Node, WindowsWindow>);
static_assert(std::is_base_of_v<Node, Window>);
static_assert(!std::is_base_of_v<Node, WindowPresenter>);
static_assert(!std::is_base_of_v<Node, ChatPresenter>);

WindowsWindow::ptr MakeWindow(ae::Domain& domain, ae::ObjId id,
                              ae::ObjId base_id) {
  auto base =
      WindowsWindow::ptr::Create(ae::CreateWith{domain}.with_id(base_id));
  auto window = WindowsWindow::ptr::Create(ae::CreateWith{domain}.with_id(id));
  window->base = base;
  window->x = 10;
  window->y = 20;
  window->width = 400;
  window->height = 300;
  window->density_dpi = 96;
  window->CaptureBaseState();
  return window;
}

WindowChangedEvent::ptr MakeEnv(ae::Domain& domain, std::int32_t work_left,
                                std::int32_t work_top, std::int32_t work_right,
                                std::int32_t work_bottom,
                                std::int32_t window_left,
                                std::int32_t window_top,
                                std::int32_t window_right,
                                std::int32_t window_bottom) {
  auto event = WindowChangedEvent::ptr::Create(ae::CreateWith{domain});
  event->available_left = work_left;
  event->available_top = work_top;
  event->available_right = work_right;
  event->available_bottom = work_bottom;
  event->window_left = window_left;
  event->window_top = window_top;
  event->window_right = window_right;
  event->window_bottom = window_bottom;
  event->density_dpi = 96;
  return event;
}

void TestA_AlreadyInsideUnchangedMaterialized() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto window = MakeWindow(domain, 1, 2);
  window->Commit(MakeEnv(domain, 0, 0, 1920, 1080, 10, 20, 410, 320));
  CHECK(window->journal.size() == 1);
  CHECK(window->x == 10);
  CHECK(window->y == 20);
  CHECK(window->width == 400);
  CHECK(window->height == 300);
}

void TestB_PartialOverflowMovesInside() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto window = MakeWindow(domain, 1, 2);
  window->Commit(MakeEnv(domain, 0, 0, 1000, 800, 900, 700, 1100, 850));
  CHECK(window->width == 200);
  CHECK(window->height == 150);
  CHECK(window->x == 800);
  CHECK(window->y == 650);
}

void TestC_LargerThanWorkAreaShrinks() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto window = MakeWindow(domain, 1, 2);
  window->Commit(MakeEnv(domain, 0, 0, 800, 600, -50, -40, 1950, 1460));
  CHECK(window->width == 800);
  CHECK(window->height == 600);
  CHECK(window->x == 0);
  CHECK(window->y == 0);
}

void TestD_NegativeWorkAreaKeepsLeftMonitor() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto window = MakeWindow(domain, 1, 2);
  window->Commit(MakeEnv(domain, -1920, 0, 0, 1080, -1800, 100, -1300, 500));
  CHECK(window->x == -1800);
  CHECK(window->y == 100);
  CHECK(window->width == 500);
  CHECK(window->height == 400);
}

void TestE_CandidateOutsideStillClamped() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto window = MakeWindow(domain, 1, 2);
  window->Commit(MakeEnv(domain, 100, 200, 900, 700, 5000, -4000, 5300, -3750));
  CHECK(window->width == 300);
  CHECK(window->height == 250);
  CHECK(window->x == 600);
  CHECK(window->y == 200);
}

void TestRepeatedCommitKeepsBothJournalEntries() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto window = MakeWindow(domain, 1, 2);
  auto event = MakeEnv(domain, 0, 0, 1920, 1080, 10, 20, 410, 320);
  window->Commit(event);
  window->Commit(MakeEnv(domain, 0, 0, 1920, 1080, 10, 20, 410, 320));
  CHECK(window->journal.size() == 2);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSingleClientChatRegistration();
  apptraverse::test::TestA_AlreadyInsideUnchangedMaterialized();
  apptraverse::test::TestB_PartialOverflowMovesInside();
  apptraverse::test::TestC_LargerThanWorkAreaShrinks();
  apptraverse::test::TestD_NegativeWorkAreaKeepsLeftMonitor();
  apptraverse::test::TestE_CandidateOutsideStillClamped();
  apptraverse::test::TestRepeatedCommitKeepsBothJournalEntries();
  std::cout << "windows_window_policy_test OK\n";
  return 0;
}
