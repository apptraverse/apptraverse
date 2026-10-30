#include "minimal_app_controls.h"

#include "minimal_application.h"

#include "minimal_app_window_events.h"

#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_push_button.h"
#include "apptraverse/ui/ui_string.h"

namespace apptraverse::example::minimal {

ae::ObjId MinimalLocaleId() { return ae::ObjId{0x07A10004u}; }
ae::ObjId MinimalWindowTitleId() { return ae::ObjId{0x07A10005u}; }
ae::ObjId MinimalApplyButtonId() { return ae::ObjId{0x07A10006u}; }
ae::ObjId MinimalApplyButtonPresenterId() { return ae::ObjId{0x07A10007u}; }
ae::ObjId MinimalRowListId() { return ae::ObjId{0x07A10008u}; }
ae::ObjId MinimalRowListPresenterId() { return ae::ObjId{0x07A10009u}; }
ae::ObjId MinimalAuxiliaryWindowId() { return ae::ObjId{0x07A1000Au}; }
ae::ObjId MinimalAuxiliaryPresenterId() { return ae::ObjId{0x07A1000Bu}; }
ae::ObjId MinimalAuxiliaryApplyButtonId() { return ae::ObjId{0x07A1000Cu}; }
ae::ObjId MinimalAuxiliaryApplyButtonPresenterId() {
  return ae::ObjId{0x07A1000Du};
}
ae::ObjId MinimalAuxiliaryRowListId() { return ae::ObjId{0x07A1000Eu}; }
ae::ObjId MinimalAuxiliaryRowListPresenterId() {
  return ae::ObjId{0x07A1000Fu};
}
ae::ObjId MinimalAuxiliaryWindowTitleId() { return ae::ObjId{0x07A10010u}; }
ae::ObjId MinimalAuxiliaryOpenerId() { return ae::ObjId{0x07A10011u}; }
ae::ObjId MinimalAuxiliaryOpenerPresenterId() {
  return ae::ObjId{0x07A10012u};
}
ae::ObjId MinimalAuxiliaryCloserId() { return ae::ObjId{0x07A10013u}; }
ae::ObjId MinimalAuxiliaryCloserPresenterId() {
  return ae::ObjId{0x07A10014u};
}

APPTRAVERSE_REGISTER(MinimalApplyButton);
APPTRAVERSE_REGISTER(MinimalAppList);
APPTRAVERSE_REGISTER(MinimalAuxiliaryOpenerButton);
APPTRAVERSE_REGISTER(MinimalAuxiliaryCloserButton);

void EnsureMinimalAppControlsRegistration() {
  (void)&g_apptraverse_registrar_MinimalApplyButton;
  (void)&g_apptraverse_registrar_MinimalAppList;
  (void)&g_apptraverse_registrar_MinimalAuxiliaryOpenerButton;
  (void)&g_apptraverse_registrar_MinimalAuxiliaryCloserButton;
}

ui::UiLocalizationString::ptr MakeMinimalLabel(ae::Domain& domain,
                                               char const* utf8) {
  auto label = ui::UiLocalizationString::ptr::Create(ae::CreateWith{domain});
  auto str = ui::UiString::ptr::Create(ae::CreateWith{domain});
  str->utf8 = utf8;
  label->strings = {str};
  return label;
}

void WireMinimalAppControlContext(MinimalApplication& application,
                                  ui::UiLocalizationString::ptr window_title) {
  application.apply_button->window_title = window_title;
  application.apply_button->row_list = application.row_list;
  application.row_list->window_title = window_title;
}

void MinimalApplyButton::HandlePushButtonClicked() {
  {
    auto title_event = ui::SetUiLocalizationSlotUtf8Event::ptr::Create(
        ae::CreateWith{*domain});
    title_event->slot_index = 0;
    title_event->utf8 = "Applied";
    window_title->Commit(std::move(title_event));
  }
  {
    auto selection_event = ui::SetUiListSelectionEvent::ptr::Create(
        ae::CreateWith{*domain});
    selection_event->selection_index = -1;
    row_list->Commit(std::move(selection_event));
  }
  {
    auto enabled_event = ui::SetUiPushButtonEnabledEvent::ptr::Create(
        ae::CreateWith{*domain});
    enabled_event->enabled = false;
    Commit(std::move(enabled_event));
  }
}

void MinimalAppList::HandleListRowActivated(std::size_t row_index) {
  char const* text =
      row_index == 0 ? "Activated Row A" : "Activated Row B";
  auto title_event = ui::SetUiLocalizationSlotUtf8Event::ptr::Create(
      ae::CreateWith{*domain});
  title_event->slot_index = 0;
  title_event->utf8 = text;
  window_title->Commit(std::move(title_event));
}

void MinimalAuxiliaryOpenerButton::HandlePushButtonClicked() {
  auto application = MinimalApplication::ptr::Declare(
      ae::CreateWith{*domain}.with_id(MinimalApplicationRootId()));
  application.Load();
  if (!application) {
    return;
  }
  auto event = MinimalOpenAuxiliaryWindowEvent::ptr::Create(
      ae::CreateWith{*domain});
  application->Commit(std::move(event));
}

void MinimalAuxiliaryCloserButton::HandlePushButtonClicked() {
  auto application = MinimalApplication::ptr::Declare(
      ae::CreateWith{*domain}.with_id(MinimalApplicationRootId()));
  application.Load();
  if (!application) {
    return;
  }
  auto event = MinimalCloseAuxiliaryWindowEvent::ptr::Create(
      ae::CreateWith{*domain});
  application->Commit(std::move(event));
}

}  // namespace apptraverse::example::minimal
