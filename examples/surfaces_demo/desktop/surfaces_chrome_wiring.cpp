#include "surfaces_chrome_wiring.h"

#include "surface_button_presenter.h"
#include "surface_language_button_presenter.h"
#include "surface_window_presenter.h"

namespace apptraverse {

void WireSurfaceChromeLocalization(Surface& surface,
                                   Application const& application) {
  if (auto window = SurfaceWindowPresenter::ptr{surface.chrome.window}) {
    window->locale = application.locale;
    window->window_title_prefix = application.window_title_prefix;
  }
  if (auto add = SurfaceButtonPresenter::ptr{surface.chrome.add_button}) {
    add->locale = application.locale;
    add->label = application.label_add;
  }
  if (auto close = SurfaceButtonPresenter::ptr{surface.chrome.close_button}) {
    close->locale = application.locale;
    close->label = application.label_close;
  }
  if (auto language = SurfaceLanguageButtonPresenter::ptr{
          surface.chrome.language_button}) {
    language->locale = application.locale;
    language->language_switch_label = application.language_switch_label;
  }
}

}  // namespace apptraverse
