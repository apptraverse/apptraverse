#include <cstring>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/windows/surfaces_win_presenters.h"
#include "apptraverse/platform/windows/win_ui_native_push_button.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/surfaces/surface_window_presenter.h"
#include "apptraverse/surfaces/surfaces_registration.h"

namespace {

void RunHelperNullParentFatal() {
  apptraverse::ui::windows::CreateChildPushButton({
      .parent_hwnd = nullptr,
      .control_id = 1,
      .create_fatal_operation = "CreateWindowExW native push button fatal test",
  });
}

void RunSurfaceButtonOnLoadFatal() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSurfacesRegistration();
  apptraverse::ui::windows::EnsureWinUiPresenterRegistration();

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto window_presenter =
      apptraverse::Win32SurfaceWindowPresenter::ptr::Create(
          ae::CreateWith{domain});
  auto button = apptraverse::Win32SurfaceButtonPresenter::ptr::Create(
      ae::CreateWith{domain});
  button->window_presenter = window_presenter;
  button->control_id = 1;
  button->OnLoad();
}

}  // namespace

int main(int argc, char** argv) {
  apptraverse::EnableNoninteractiveCrt();
  if (argc >= 2 && std::strcmp(argv[1], "surface_button_onload") == 0) {
    RunSurfaceButtonOnLoadFatal();
    return 0;
  }
  RunHelperNullParentFatal();
  return 0;
}
