#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_desktop_window.h"

using namespace apptraverse;
using namespace apptraverse::ui;

namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (false)

void TestRestoreVsLiveClientReports(UiDesktopWindow& desktop) {
  desktop.SubmitFrameFromNative(100, 120, 800, 600, true);
  auto const gen_after_first = desktop.Generation();
  desktop.SubmitFrameFromNative(100, 120, 800, 600, true);
  CHECK(desktop.Generation() == gen_after_first);
  CHECK(desktop.client_width() == 800);
  CHECK(desktop.client_height() == 600);
  CHECK(desktop.restore_client_width() == 800);
  CHECK(desktop.restore_client_height() == 600);
  CHECK(desktop.x() == 100);
  CHECK(desktop.y() == 120);

  desktop.SubmitFrameFromNative(0, 0, 1920, 1040, false);
  CHECK(desktop.client_width() == 1920);
  CHECK(desktop.client_height() == 1040);
  CHECK(desktop.restore_client_width() == 800);
  CHECK(desktop.restore_client_height() == 600);
  CHECK(desktop.x() == 100);
  CHECK(desktop.y() == 120);

  desktop.SubmitFrameFromNative(0, 0, 1600, 900, false);
  CHECK(desktop.client_width() == 1600);
  CHECK(desktop.client_height() == 900);
  CHECK(desktop.restore_client_width() == 800);
  CHECK(desktop.restore_client_height() == 600);
}

void TestUserResizeUpdatesRestore(UiDesktopWindow& desktop) {
  desktop.SubmitFrameFromNative(140, 160, 820, 610, true);
  CHECK(desktop.restore_client_width() == 820);
  CHECK(desktop.restore_client_height() == 610);
  CHECK(desktop.client_width() == 820);
}

void TestExplicitRequestUpdatesRestoreAndLive(UiDesktopWindow& desktop) {
  desktop.RequestFrame(30, 40, 700, 500);
  CHECK(desktop.x() == 30);
  CHECK(desktop.y() == 40);
  CHECK(desktop.restore_client_width() == 700);
  CHECK(desktop.restore_client_height() == 500);
  CHECK(desktop.client_width() == 700);
  CHECK(desktop.client_height() == 500);
  CHECK(desktop.TakeFrameRequestPendingPublication());
}

void TestColdLoadPreservesRestoreGeometry(UiDesktopWindow& desktop) {
  desktop.SubmitFrameFromNative(100, 120, 800, 600, true);
  desktop.SubmitFrameFromNative(0, 0, 1600, 900, false);
  UiDesktopWindow::ptr::MakeFromThis(&desktop).Save();
  UiDesktopWindow::ptr::MakeFromThis(&desktop).Load();
  CHECK(desktop.restore_client_width() == 800);
  CHECK(desktop.restore_client_height() == 600);
  CHECK(desktop.x() == 100);
  CHECK(desktop.y() == 120);
  CHECK(desktop.client_width() == 1600);
  CHECK(desktop.client_height() == 900);
}

}  // namespace

int main() {
  EnableNoninteractiveCrt();
  EnsureObjectRegistration();

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto doc = UiWindow::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*doc);
  auto desktop = UiDesktopWindow::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*desktop, *doc);

  TestRestoreVsLiveClientReports(*desktop);
  TestUserResizeUpdatesRestore(*desktop);
  TestExplicitRequestUpdatesRestoreAndLive(*desktop);
  TestColdLoadPreservesRestoreGeometry(*desktop);

  return 0;
}
