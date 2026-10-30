#include "minimal_app_graph_builder.h"

#include "minimal_app_controls.h"
#include "minimal_app_window_lifecycle.h"

#include "apptraverse/node.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_presented_window_composition.h"

namespace apptraverse::example::minimal {
namespace {

struct MinimalWindowPanelIds {
  ae::ObjId window_id;
  ae::ObjId window_presenter_id;
  ae::ObjId apply_button_id;
  ae::ObjId apply_button_presenter_id;
  ae::ObjId row_list_id;
  ae::ObjId row_list_presenter_id;
  std::uint32_t apply_control_id;
  std::uint32_t list_control_id;
};

struct MinimalWindowPanelPresenters {
  ui::UiPushButtonPresenter::ptr apply_button;
  ui::UiListContainerPresenter::ptr row_list;
};

struct MinimalWindowPanelGraph {
  ui::UiDesktopWindow::ptr window;
  ui::UiDesktopWindowPresenter::ptr window_presenter;
  MinimalApplyButton::ptr apply_button;
  MinimalAppList::ptr row_list;
  MinimalWindowPanelPresenters presenters;
};

MinimalWindowPanelGraph ComposeMinimalWindowPanel(
    ae::Domain& domain, Node& parent,
    ui::UiLocalizationString::ptr title,
    ui::UiLocaleSettings::ptr locale,
    MinimalPresenterFactories const& factories,
    MinimalWindowPanelIds const& ids, char const* row_a_utf8,
    char const* row_b_utf8, std::int32_t layout_apply_y) {
  MinimalWindowPanelGraph panel;
  panel.window = ui::UiDesktopWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(ids.window_id));
  panel.window_presenter = factories.create_window(domain, ids.window_presenter_id);
  ui::ComposePresentedWindow(panel.window, panel.window_presenter, title, locale);
  InitializeRuntimeNode(*panel.window, parent);

  panel.apply_button = MinimalApplyButton::ptr::Create(
      ae::CreateWith{domain}.with_id(ids.apply_button_id));
  auto apply_label = MakeMinimalLabel(domain, "Apply");
  InitializeRuntimeNode(*apply_label, *panel.apply_button);
  panel.apply_button->label = apply_label;
  InitializeRuntimeNode(*panel.apply_button, *panel.window);

  panel.row_list = MinimalAppList::ptr::Create(
      ae::CreateWith{domain}.with_id(ids.row_list_id));
  panel.row_list->selection_index = -1;
  InitializeRuntimeNode(*panel.row_list, *panel.window);
  {
    auto row0 = ui::UiListRow::ptr::Create(ae::CreateWith{domain});
    auto row1 = ui::UiListRow::ptr::Create(ae::CreateWith{domain});
    InitializeRuntimeNode(*row0, *panel.row_list);
    InitializeRuntimeNode(*row1, *panel.row_list);
    row0->primary_text = MakeMinimalLabel(domain, row_a_utf8);
    InitializeRuntimeNode(*row0->primary_text, *row0);
    row1->primary_text = MakeMinimalLabel(domain, row_b_utf8);
    InitializeRuntimeNode(*row1->primary_text, *row1);
    panel.row_list->rows = {row0, row1};
  }

  panel.apply_button->window_title = title;
  panel.apply_button->row_list = panel.row_list;
  panel.row_list->window_title = title;

  auto button_presenter = factories.create_button(
      domain, ids.apply_button_presenter_id);
  button_presenter->button = panel.apply_button;
  button_presenter->window_presenter = panel.window_presenter;
  button_presenter->layout_x = 8;
  button_presenter->layout_y = layout_apply_y;
  button_presenter->layout_width = 100;
  button_presenter->layout_height = 26;
  button_presenter->control_id = ids.apply_control_id;

  auto list_presenter = factories.create_list(domain, ids.row_list_presenter_id);
  list_presenter->container = panel.row_list;
  list_presenter->window_presenter = panel.window_presenter;
  list_presenter->layout_x = 8;
  list_presenter->layout_y = layout_apply_y + 32;
  list_presenter->layout_width = 200;
  list_presenter->layout_height = 120;
  list_presenter->control_id = ids.list_control_id;
  list_presenter->notify_list_selection_change = true;

  panel.presenters.apply_button = button_presenter;
  panel.presenters.row_list = list_presenter;

  return panel;
}

void CommitInitialFrame(ui::UiDesktopWindow& window, ae::Domain& domain,
                        std::int32_t x, std::int32_t y, std::int32_t cw,
                        std::int32_t ch) {
  auto frame = ui::SetUiDesktopWindowFrameEvent::ptr::Create(
      ae::CreateWith{domain});
  frame->x = x;
  frame->y = y;
  frame->client_width = cw;
  frame->client_height = ch;
  window.Commit(std::move(frame));
}

}  // namespace

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

  MinimalWindowPanelIds main_ids{
      MinimalMainWindowId(),
      MinimalPresenterId(),
      MinimalApplyButtonId(),
      MinimalApplyButtonPresenterId(),
      MinimalRowListId(),
      MinimalRowListPresenterId(),
      kMinimalApplyButtonControlId,
      kMinimalRowListControlId,
  };
  MinimalWindowPanelGraph main_panel = ComposeMinimalWindowPanel(
      domain, *application, title, locale, factories, main_ids, "Row A",
      "Row B", 8);
  application->main_window = main_panel.window;
  application->apply_button = main_panel.apply_button;
  application->row_list = main_panel.row_list;
  application->presenters.apply_button = main_panel.presenters.apply_button;
  application->presenters.row_list = main_panel.presenters.row_list;

  application->auxiliary_opener = MinimalAuxiliaryOpenerButton::ptr::Create(
      ae::CreateWith{domain}.with_id(MinimalAuxiliaryOpenerId()));
  auto opener_label = ui::UiLocalizationString::ptr::Create(
      ae::CreateWith{domain}.with_id(MinimalAuxiliaryOpenerLabelId()));
  {
    auto opener_str = ui::UiString::ptr::Create(ae::CreateWith{domain});
    opener_str->utf8 = "Open Detail";
    opener_label->strings = {opener_str};
  }
  InitializeRuntimeNode(*opener_label, *application->auxiliary_opener);
  application->auxiliary_opener->label = opener_label;
  application->auxiliary_opener->window_title = title;
  application->auxiliary_opener->row_list = application->row_list;
  InitializeRuntimeNode(*application->auxiliary_opener, *main_panel.window);

  auto opener_presenter = factories.create_button(
      domain, MinimalAuxiliaryOpenerPresenterId());
  opener_presenter->button = application->auxiliary_opener;
  opener_presenter->window_presenter = main_panel.window_presenter;
  opener_presenter->layout_x = 120;
  opener_presenter->layout_y = 8;
  opener_presenter->layout_width = 100;
  opener_presenter->layout_height = 26;
  opener_presenter->control_id = kMinimalAuxiliaryOpenerControlId;
  application->presenters.auxiliary_opener = opener_presenter;

  auto aux_title_str = ui::UiString::ptr::Create(ae::CreateWith{domain});
  aux_title_str->utf8 = "Detail Panel";
  auto aux_title = ui::UiLocalizationString::ptr::Create(
      ae::CreateWith{domain}.with_id(MinimalAuxiliaryWindowTitleId()));
  aux_title->strings = {aux_title_str};
  InitializeRuntimeNode(*aux_title, *application);

  MinimalWindowPanelIds aux_ids{
      MinimalAuxiliaryWindowId(),
      MinimalAuxiliaryPresenterId(),
      MinimalAuxiliaryApplyButtonId(),
      MinimalAuxiliaryApplyButtonPresenterId(),
      MinimalAuxiliaryRowListId(),
      MinimalAuxiliaryRowListPresenterId(),
      kMinimalAuxiliaryApplyButtonControlId,
      kMinimalAuxiliaryRowListControlId,
  };
  MinimalWindowPanelGraph aux_panel = ComposeMinimalWindowPanel(
      domain, *application, aux_title, locale, factories, aux_ids, "Detail A",
      "Detail B", 40);
  application->auxiliary.window = aux_panel.window;
  application->auxiliary.apply_button = aux_panel.apply_button;
  application->auxiliary.row_list = aux_panel.row_list;
  application->auxiliary.presenters.apply_button =
      aux_panel.presenters.apply_button;
  application->auxiliary.presenters.row_list = aux_panel.presenters.row_list;

  application->auxiliary.closer = MinimalAuxiliaryCloserButton::ptr::Create(
      ae::CreateWith{domain}.with_id(MinimalAuxiliaryCloserId()));
  auto closer_label = ui::UiLocalizationString::ptr::Create(
      ae::CreateWith{domain}.with_id(MinimalAuxiliaryCloserLabelId()));
  {
    auto closer_str = ui::UiString::ptr::Create(ae::CreateWith{domain});
    closer_str->utf8 = "Close Detail";
    closer_label->strings = {closer_str};
  }
  InitializeRuntimeNode(*closer_label, *application->auxiliary.closer);
  application->auxiliary.closer->label = closer_label;
  application->auxiliary.closer->window_title = aux_title;
  InitializeRuntimeNode(*application->auxiliary.closer, *aux_panel.window);

  auto closer_presenter = factories.create_button(
      domain, MinimalAuxiliaryCloserPresenterId());
  closer_presenter->button = application->auxiliary.closer;
  closer_presenter->window_presenter = aux_panel.window_presenter;
  closer_presenter->layout_x = 8;
  closer_presenter->layout_y = 8;
  closer_presenter->layout_width = 100;
  closer_presenter->layout_height = 26;
  closer_presenter->control_id = kMinimalAuxiliaryCloserControlId;
  application->auxiliary.presenters.closer = closer_presenter;

  application->auxiliary_presented_open = false;
  InitializeRuntimeNode(*application);

  CommitInitialFrame(*main_panel.window, domain, 120, 80, 640, 480);
  CommitInitialFrame(*aux_panel.window, domain, 280, 120, 480, 360);

  SealAuxiliaryWindowClosed(*application);

  return application;
}

}  // namespace apptraverse::example::minimal
