#include "apptraverse/object_macros.h"

#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_edit_box.h"
#include "apptraverse/ui/ui_label.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_push_button.h"
#include "apptraverse/ui/ui_string.h"
#include "apptraverse/ui/ui_window.h"

namespace apptraverse::ui {
namespace {

APPTRAVERSE_REGISTER(UiString);
APPTRAVERSE_REGISTER(UiLocalizationString);
APPTRAVERSE_REGISTER(UiLocaleSettings);
APPTRAVERSE_REGISTER(SetUiLanguageIndexEvent);
APPTRAVERSE_REGISTER(UiWindow);
APPTRAVERSE_REGISTER(SetUiWindowClientSizeEvent);
APPTRAVERSE_REGISTER(UiDesktopWindow);
APPTRAVERSE_REGISTER(SetUiDesktopWindowFrameEvent);
APPTRAVERSE_REGISTER(UiEditBox);
APPTRAVERSE_REGISTER(SetUiEditBoxTextEvent);
APPTRAVERSE_REGISTER(SetUiEditBoxCaretEvent);
APPTRAVERSE_REGISTER(UiPushButton);
APPTRAVERSE_REGISTER(UiPushButtonClickedEvent);
APPTRAVERSE_REGISTER(UiLabel);
APPTRAVERSE_REGISTER(UiListRow);
APPTRAVERSE_REGISTER(UiListContainer);
APPTRAVERSE_REGISTER(RemoveUiListRowEvent);
APPTRAVERSE_REGISTER(UiListRowPresenter);
APPTRAVERSE_REGISTER(UiDesktopWindowPresenter);
APPTRAVERSE_REGISTER(UiEditBoxPresenter);
APPTRAVERSE_REGISTER(UiPushButtonPresenter);
APPTRAVERSE_REGISTER(UiLabelPresenter);

}  // namespace

void EnsureUiPresenterRegistration() {
  (void)&g_apptraverse_registrar_UiDesktopWindowPresenter;
  (void)&g_apptraverse_registrar_UiEditBoxPresenter;
  (void)&g_apptraverse_registrar_UiPushButtonPresenter;
  (void)&g_apptraverse_registrar_UiLabelPresenter;
  (void)&g_apptraverse_registrar_UiListRowPresenter;
}

void ForceUiRegistration() {
  (void)&g_apptraverse_registrar_UiString;
  EnsureUiPresenterRegistration();
}

}  // namespace apptraverse::ui
