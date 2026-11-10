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

void TestInitialPlacement(ae::Domain& domain) {
  auto window = UiDesktopWindow::ptr::Create(ae::CreateWith{domain});
  window->BootstrapFrame(0, 0, 640, 480);
  InitializeRuntimeNode(*window);
  CHECK(!window->HasExplicitPosition());
  window->SubmitFrameFromNative(45, 67, 640, 480, true);
  CHECK(window->HasExplicitPosition());
  CHECK(window->x() == 45 && window->y() == 67);
  CHECK(!window->TakeFrameRequestPendingPublication());
  window.Save();
  ae::Domain cold{domain.domain_storage()};
  auto restored = UiDesktopWindow::ptr::Declare(ae::CreateWith{cold}.with_id(window.id()));
  auto loaded = restored.Load();
  CHECK(loaded && loaded->HasExplicitPosition());
  CHECK(loaded->x() == 45 && loaded->y() == 67);

  auto centered = UiDesktopWindow::ptr::Create(ae::CreateWith{domain});
  centered->BootstrapFrame(0, 0, 640, 480, UiDesktopWindow::InitialPlacement::Centered);
  InitializeRuntimeNode(*centered);
  centered->SubmitFrameFromNative(12, 34, 640, 480, true);
  centered->SubmitScreenFromNative(-1200, 0, 1200, 900);
  CHECK(centered->x() == -920 && centered->y() == 210);
  CHECK(centered->TakeFrameRequestPendingPublication());
  centered->SubmitScreenFromNative(0, 0, 1920, 1080);
  CHECK(centered->x() == -920 && centered->y() == 210);
  CHECK(!centered->TakeFrameRequestPendingPublication());

}

}  // namespace

int main() {
  EnableNoninteractiveCrt();
  EnsureObjectRegistration();

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto doc = UiWindow::ptr::Create(ae::CreateWith{domain});
  doc->BootstrapClientSize(800, 600);
  InitializeRuntimeNode(*doc);
  auto desktop = UiDesktopWindow::ptr::Create(ae::CreateWith{domain});
  desktop->BootstrapFrame(100, 100, 800, 600, apptraverse::ui::UiDesktopWindow::InitialPlacement::Explicit);
  InitializeRuntimeNode(*desktop, *doc);

  TestInitialPlacement(domain);
  TestRestoreVsLiveClientReports(*desktop);
  TestUserResizeUpdatesRestore(*desktop);
  TestExplicitRequestUpdatesRestoreAndLive(*desktop);
  TestColdLoadPreservesRestoreGeometry(*desktop);

  return 0;
}
