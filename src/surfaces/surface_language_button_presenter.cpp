#include "apptraverse/surfaces/surface_language_button_presenter.h"

#include "apptraverse/model_object_proxy.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(SurfaceLanguageButtonPresenter);

}  // namespace

void EnsureSurfaceLanguageButtonPresenterRegistration() {
  (void)&g_apptraverse_registrar_SurfaceLanguageButtonPresenter;
}

bool SurfaceLanguageButtonPresenter::ReadyForPresentation() const {
  return window_presenter && window_presenter->presentation_loaded;
}

bool SurfaceLanguageButtonPresenter::OnCommand(std::uint32_t command_id,
                                               std::uint16_t notification_code) {
  (void)notification_code;
  if (command_id != control_id || !model_proxy || !locale) {
    return false;
  }
  int const next = 1 - locale->language_index();
  model_proxy->Invoke<ui::UiLocaleSettings>(
      locale->obj_id, &ui::UiLocaleSettings::SubmitLanguageIndexFromUi, next);
  return true;
}

}  // namespace apptraverse
