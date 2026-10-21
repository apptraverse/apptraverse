#include "apptraverse/surfaces/surfaces_chrome_wiring.h"

#include <cstdint>

#include "aether-objects/obj/registry.h"

#include "apptraverse/surfaces/surface_button_presenter.h"
#include "apptraverse/surfaces/surface_language_button_presenter.h"
#include "apptraverse/surfaces/surface_window_presenter.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_push_button_presenter.h"

namespace apptraverse {
namespace {

bool IsClassOrDerived(std::uint32_t base, std::uint32_t derived) {
  return ae::Registry::GetRegistry().GenerationDistance(base, derived) >= 0;
}

}  // namespace

void WireSurfaceChromeLocalization(Surface& surface,
                                   Application const& application) {
  if (auto window = SurfaceWindowPresenter::ptr{surface.chrome.window}) {
    window->locale = application.locale;
    window->window_title_prefix = application.window_title_prefix;
  }
  if (surface.chrome.add_button.is_valid()) {
    std::uint32_t const add_class = surface.chrome.add_button->GetClassId();
    if (IsClassOrDerived(SurfaceButtonPresenter::kClassId, add_class)) {
      auto add = SurfaceButtonPresenter::ptr{surface.chrome.add_button};
      add->locale = application.locale;
      add->label = application.label_add;
    } else if (IsClassOrDerived(ui::UiPushButtonPresenter::kClassId,
                                add_class)) {
      auto library_add = ui::UiPushButtonPresenter::ptr{
          surface.chrome.add_button};
      if (library_add->button) {
        library_add->button->label = application.label_add;
      }
      if (library_add->window_presenter) {
        library_add->window_presenter->locale = application.locale;
      }
    }
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
