#include "minimal_app_graph.h"

#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_presented_window_composition.h"

namespace apptraverse::example::minimal {

MinimalApplication::ptr BuildMinimalApplicationGraph(ae::Domain& domain) {
  ui::windows::EnsureWinUiPresenterRegistration();
  auto application = MinimalApplication::ptr::Create(
      ae::CreateWith{domain}.with_id(MinimalApplicationRootId()));

  auto window = ui::UiDesktopWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(MinimalMainWindowId()));
  auto presenter =
      ui::windows::WinDesktopWindowPresenter::ptr::Create(
          ae::CreateWith{domain}.with_id(MinimalPresenterId()));

  ui::ComposePresentedWindow(window, presenter, ui::UiLocalizationString::ptr{},
                           ui::UiLocaleSettings::ptr{});

  InitializeRuntimeNode(*window, *application);
  application->main_window = window;
  InitializeRuntimeNode(*application);

  auto frame = ui::SetUiDesktopWindowFrameEvent::ptr::Create(
      ae::CreateWith{domain});
  frame->x = 120;
  frame->y = 80;
  frame->client_width = 640;
  frame->client_height = 480;
  window->Commit(std::move(frame));
  return application;
}

}  // namespace apptraverse::example::minimal
