#include "apptraverse/platform/windows/win_ui_presenters.h"

#include <cstdlib>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/runtime_node.h"

#define CHECK(x) do { if (!(x)) { std::cerr << "CHECK failed: " #x << '\n'; std::abort(); } } while (false)

int main() {
  using namespace apptraverse;
  using namespace apptraverse::ui;
  using namespace apptraverse::ui::windows;
  EnsureObjectRegistration();
  EnsureWinUiPresenterRegistration();
  EnsureExtendedWinUiPresenterRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto window = UiDesktopWindow::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*window);
  auto desktop = WinDesktopWindowPresenter::ptr::Create(ae::CreateWith{domain});
  desktop->window = window;
  std::size_t commands = 0;
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) {
    ++commands;
    work(domain);
  }};
  desktop->model_proxy = &proxy;
  desktop->OnLoad();
  desktop->presentation_loaded = true;
  auto edit = UiEditBox::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*edit);
  auto presenter = WinEditBoxPresenter::ptr::Create(ae::CreateWith{domain});
  presenter->edit_box = edit;
  presenter->window_presenter = desktop;
  presenter->layout_x = 10; presenter->layout_y = 10;
  presenter->layout_width = 100; presenter->layout_height = 24;
  presenter->control_id = 41;
  presenter->model_proxy = &proxy;
  presenter->OnLoad();
  CHECK(presenter->hwnd() != nullptr);
  commands = 0;
  CHECK(SetWindowTextW(presenter->hwnd(), L"route through instance") != 0);
  CHECK(edit->text() == "route through instance");
  CHECK(commands == 1);
  CHECK(!desktop->RouteChildCommand(42, EN_CHANGE, presenter->hwnd()));
  CHECK(commands == 1);
  // Registration cannot replace a window's handler; dispatch is virtual on
  // the concrete child, including when the optional module is registered again.
  EnsureExtendedWinUiPresenterRegistration();
  CHECK(SetWindowTextW(presenter->hwnd(), L"second input") != 0);
  CHECK(edit->text() == "second input");
  CHECK(commands == 2);
  presenter->OnUnload();
  desktop->OnUnload();
  std::cout << "win_ui_command_routing_test OK\n";
}
