#include "apptraverse/object_macros.h"

#include "apptraverse/ui/ui_bitmap_image.h"
#include "apptraverse/ui/ui_edit_box.h"
#include "apptraverse/ui/ui_label.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui {
namespace {

APPTRAVERSE_REGISTER(UiEditBox);
APPTRAVERSE_REGISTER(SetUiEditBoxTextEvent);
APPTRAVERSE_REGISTER(SetUiEditBoxCaretEvent);
APPTRAVERSE_REGISTER(UiLabel);
APPTRAVERSE_REGISTER(UiBitmapImage);
APPTRAVERSE_REGISTER(UiBitmapImagePresenter);
APPTRAVERSE_REGISTER(UiEditBoxPresenter);
APPTRAVERSE_REGISTER(UiLabelPresenter);

}  // namespace

void EnsureExtendedUiPresenterRegistration() {
  (void)&g_apptraverse_registrar_UiEditBoxPresenter;
  (void)&g_apptraverse_registrar_UiLabelPresenter;
  (void)&g_apptraverse_registrar_UiBitmapImagePresenter;
}

}  // namespace apptraverse::ui
