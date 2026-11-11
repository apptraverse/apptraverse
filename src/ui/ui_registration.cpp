#include "apptraverse/object_macros.h"

#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_presented_window.h"
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
APPTRAVERSE_REGISTER(SetUiLocalizationSlotUtf8Event);
APPTRAVERSE_REGISTER(UiLocaleSettings);
APPTRAVERSE_REGISTER(SetUiLanguageIndexEvent);
APPTRAVERSE_REGISTER(UiWindow);
APPTRAVERSE_REGISTER(SetUiWindowClientSizeEvent);
APPTRAVERSE_REGISTER(UiPresentedWindow);
APPTRAVERSE_REGISTER(UiDesktopWindow);
APPTRAVERSE_REGISTER(SetUiDesktopWindowFrameEvent);
APPTRAVERSE_REGISTER(ReportUiDesktopWindowFrameFromNativeEvent);
APPTRAVERSE_REGISTER(ReportUiDesktopScreenEvent);
APPTRAVERSE_REGISTER(RequestUiDesktopWindowFrameEvent);
APPTRAVERSE_REGISTER(UiPushButton);
APPTRAVERSE_REGISTER(UiPushButtonClickedEvent);
APPTRAVERSE_REGISTER(SetUiPushButtonEnabledEvent);
APPTRAVERSE_REGISTER(UiListRow);
APPTRAVERSE_REGISTER(UiListContainer);
APPTRAVERSE_REGISTER(SetUiListSelectionEvent);
APPTRAVERSE_REGISTER(RemoveUiListRowEvent);
APPTRAVERSE_REGISTER(UiWindowPresenter);
APPTRAVERSE_REGISTER(UiDesktopWindowPresenter);
APPTRAVERSE_REGISTER(UiPushButtonPresenter);
APPTRAVERSE_REGISTER(UiListContainerPresenter);

}  // namespace

void EnsureUiControlRegistration() {
  (void)&g_apptraverse_registrar_UiString;
  (void)&g_apptraverse_registrar_UiPresentedWindow;
}

}  // namespace apptraverse::ui
