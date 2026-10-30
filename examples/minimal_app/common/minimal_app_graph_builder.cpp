#include "minimal_app_graph_builder.h"

#include "minimal_app_controls.h"

#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_presented_window_composition.h"

namespace apptraverse::example::minimal {

MinimalApplication::ptr BuildMinimalApplicationGraph(
    ae::Domain& domain, MinimalPresenterFactories const& factories) {
  auto application = MinimalApplication::ptr::Create(
      ae::CreateWith{domain}.with_id(MinimalApplicationRootId()));

  auto locale = ui::UiLocaleSettings::ptr::Create(
      ae::CreateWith{domain}.with_id(MinimalLocaleId()));
  InitializeRuntimeNode(*locale, *application);

  auto title_str = ui::UiString::ptr::Create(ae::CreateWith{domain});
  title_str->utf8 = "Minimal App";
  auto title = ui::UiLocalizationString::ptr::Create(
      ae::CreateWith{domain}.with_id(MinimalWindowTitleId()));
  title->strings = {title_str};
  InitializeRuntimeNode(*title, *application);

  auto window = ui::UiDesktopWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(MinimalMainWindowId()));
  ui::UiDesktopWindowPresenter::ptr window_presenter =
      factories.create_window(domain, MinimalPresenterId());
  ui::ComposePresentedWindow(window, window_presenter, title, locale);
  InitializeRuntimeNode(*window, *application);
  application->main_window = window;

  application->apply_button = MinimalApplyButton::ptr::Create(
      ae::CreateWith{domain}.with_id(MinimalApplyButtonId()));
  auto apply_label = MakeMinimalLabel(domain, "Apply");
  InitializeRuntimeNode(*apply_label, *application->apply_button);
  application->apply_button->label = apply_label;
  InitializeRuntimeNode(*application->apply_button, *window);

  application->row_list = MinimalAppList::ptr::Create(
      ae::CreateWith{domain}.with_id(MinimalRowListId()));
  application->row_list->selection_index = -1;
  InitializeRuntimeNode(*application->row_list, *window);
  {
    auto row0 = ui::UiListRow::ptr::Create(ae::CreateWith{domain});
    auto row1 = ui::UiListRow::ptr::Create(ae::CreateWith{domain});
    InitializeRuntimeNode(*row0, *application->row_list);
    InitializeRuntimeNode(*row1, *application->row_list);
    row0->primary_text = MakeMinimalLabel(domain, "Row A");
    InitializeRuntimeNode(*row0->primary_text, *row0);
    row1->primary_text = MakeMinimalLabel(domain, "Row B");
    InitializeRuntimeNode(*row1->primary_text, *row1);
    application->row_list->rows = {row0, row1};
  }

  WireMinimalAppControlContext(*application, title);

  auto button_presenter = factories.create_button(
      domain, MinimalApplyButtonPresenterId());
  button_presenter->button = application->apply_button;
  button_presenter->window_presenter = window_presenter;
  button_presenter->layout_x = 8;
  button_presenter->layout_y = 8;
  button_presenter->layout_width = 100;
  button_presenter->layout_height = 26;
  button_presenter->control_id = kMinimalApplyButtonControlId;

  auto list_presenter = factories.create_list(domain, MinimalRowListPresenterId());
  list_presenter->container = application->row_list;
  list_presenter->window_presenter = window_presenter;
  list_presenter->layout_x = 8;
  list_presenter->layout_y = 40;
  list_presenter->layout_width = 200;
  list_presenter->layout_height = 120;
  list_presenter->control_id = kMinimalRowListControlId;
  list_presenter->notify_list_selection_change = true;

  application->presenters.apply_button = button_presenter;
  application->presenters.row_list = list_presenter;

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
