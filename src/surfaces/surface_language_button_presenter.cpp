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
  return window_presenter->presentation_loaded;
}

void SurfaceLanguageButtonPresenter::NotifyClickedFromUi() {
  int const next = 1 - locale->language_index();
  model_proxy->Invoke<ui::UiLocaleSettings>(
      locale->obj_id, &ui::UiLocaleSettings::SubmitLanguageIndexFromUi, next);
}

}  // namespace apptraverse
