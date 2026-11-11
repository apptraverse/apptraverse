#include <cstring>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_serialization.h"
#include "window_title_fixture.h"
#include "apptraverse/platform/windows/surfaces_win_presenters.h"
#include "apptraverse/platform/windows/win_ui_native_push_button.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/surfaces/surface_window_presenter.h"
#include "apptraverse/surfaces/surfaces_registration.h"

namespace {

void RunListCreateFatal() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto parent = apptraverse::ui::windows::WinDesktopWindowPresenter::ptr::Create(
      ae::CreateWith{domain});
  auto list = apptraverse::ui::windows::WinListContainerPresenter::ptr::Create(
      ae::CreateWith{domain});
  list->window_presenter = parent;
  list->control_id = 1;
  list->notify_list_selection_change = false;
  list->layout_x = list->layout_y = 0;
  list->layout_width = list->layout_height = 100;
  // Deliberately unavailable native parent: OnLoad must not return success.
  list->OnLoad();
}

LRESULT CALLBACK RejectWindowCreate(int code, WPARAM, LPARAM) {
  return code == HCBT_CREATEWND ? 1 : 0;
}

void RunDesktopCreateFatal() {
  apptraverse::EnsureObjectRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto window = apptraverse::ui::UiDesktopWindow::ptr::Create(ae::CreateWith{domain});
  window->BootstrapFrame(0, 0, 320, 240,
      apptraverse::ui::UiDesktopWindow::InitialPlacement::Explicit);
  auto presenter = apptraverse::ui::windows::WinDesktopWindowPresenter::ptr::Create(
      ae::CreateWith{domain});
  presenter->window = window;
  apptraverse::test::BuildEmptyWindowTitle(*presenter);
  HHOOK const hook = SetWindowsHookExW(WH_CBT, RejectWindowCreate, nullptr,
                                      GetCurrentThreadId());
  if (!hook) { std::exit(2); }
  presenter->OnLoad();
  UnhookWindowsHookEx(hook);
}

void RunRequiredPublicationFatal() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  std::uint8_t const empty_envelope[] = {0};
  apptraverse::ByteSource in;
  in.data = empty_envelope;
  in.size = sizeof(empty_envelope);
  (void)apptraverse::LoadInitialPublication(in, domain, storage);
}

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
  if (argc >= 2 && std::strcmp(argv[1], "list_create") == 0) {
    RunListCreateFatal();
    return 0;
  }
  if (argc >= 2 && std::strcmp(argv[1], "desktop_create") == 0) {
    RunDesktopCreateFatal();
    return 0;
  }
  if (argc >= 2 && std::strcmp(argv[1], "required_publication") == 0) {
    RunRequiredPublicationFatal();
    return 0;
  }
  if (argc >= 2 && std::strcmp(argv[1], "surface_button_onload") == 0) {
    RunSurfaceButtonOnLoadFatal();
    return 0;
  }
  RunHelperNullParentFatal();
  return 0;
}
