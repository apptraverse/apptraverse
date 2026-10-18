#ifndef APPTRAVERSE_SURFACE_LANGUAGE_BUTTON_PRESENTER_H_
#define APPTRAVERSE_SURFACE_LANGUAGE_BUTTON_PRESENTER_H_

#include "apptraverse/object_macros.h"
#include "apptraverse/presenter.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_locale_settings.h"

#include "surface_window_presenter.h"
#include "surfaces_model.h"

namespace apptraverse {

// Toggles shared UiLocaleSettings for the whole surfaces demo app.
class SurfaceLanguageButtonPresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::SurfaceLanguageButtonPresenter",
      SurfaceLanguageButtonPresenter, Presenter, 0)

 protected:
  SurfaceLanguageButtonPresenter() = default;

 public:
  explicit SurfaceLanguageButtonPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(surface), AE_MMBR(window_presenter),
                    AE_MMBR(locale), AE_MMBR(language_switch_label),
                    AE_MMBR(control_id))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, surface, window_presenter, locale, language_switch_label,
        control_id);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, surface, window_presenter, locale, language_switch_label,
        control_id);
  }

  bool ReadyForPresentation() const override;
  bool OnCommand(std::uint32_t command_id,
                 std::uint16_t notification_code) override;

  Surface::ptr surface;
  ae::ObjPtr<SurfaceWindowPresenter> window_presenter;
  ae::ObjPtr<ui::UiLocaleSettings> locale;
  ae::ObjPtr<ui::UiLocalizationString> language_switch_label;
  std::uint32_t control_id{0};
};

void EnsureSurfaceLanguageButtonPresenterRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACE_LANGUAGE_BUTTON_PRESENTER_H_
