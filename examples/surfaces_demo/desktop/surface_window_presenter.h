#ifndef APPTRAVERSE_SURFACE_WINDOW_PRESENTER_H_
#define APPTRAVERSE_SURFACE_WINDOW_PRESENTER_H_

#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_locale_settings.h"

#include "desktop_surface_presenter.h"

namespace apptraverse {

// One native window per Surface. Platform siblings: Win32SurfaceWindowPresenter,
// MacSurfacePresenter, LinuxSurfacePresenter.
class SurfaceWindowPresenter : public DesktopSurfacePresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::SurfaceWindowPresenter",
      SurfaceWindowPresenter, DesktopSurfacePresenter, 0)

 protected:
  SurfaceWindowPresenter() = default;

 public:
  explicit SurfaceWindowPresenter(ae::ObjProp prop)
      : DesktopSurfacePresenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(locale), AE_MMBR(window_title_prefix))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, locale, window_title_prefix);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, locale, window_title_prefix);
  }

  ae::ObjPtr<ui::UiLocaleSettings> locale;
  ae::ObjPtr<ui::UiLocalizationString> window_title_prefix;

  void PageShown();
  void PresentationSizeChanged(std::int32_t width, std::int32_t height);
};

void EnsureSurfaceWindowPresenterRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACE_WINDOW_PRESENTER_H_
