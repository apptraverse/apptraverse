#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_desktop_window.h"

using namespace apptraverse;
using namespace apptraverse::ui;

int main() {
  EnableNoninteractiveCrt();
  EnsureObjectRegistration();

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto doc = UiWindow::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*doc);
  auto desktop = UiDesktopWindow::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*desktop, *doc);

  desktop->SubmitFrameFromNative(10, 20, 640, 480);
  if (desktop->x() != 10 || desktop->y() != 20 ||
      desktop->client_width() != 640 || desktop->client_height() != 480) {
    std::cerr << "frame apply failed\n";
    return 1;
  }
  auto const gen1 = desktop->Generation();
  desktop->SubmitFrameFromNative(10, 20, 640, 480);
  if (desktop->Generation() != gen1) {
    std::cerr << "duplicate frame must not bump generation\n";
    return 2;
  }
  return 0;
}
