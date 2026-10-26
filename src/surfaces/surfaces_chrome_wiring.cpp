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
  if (surface.chrome.close_button.is_valid()) {
    std::uint32_t const close_class = surface.chrome.close_button->GetClassId();
    if (IsClassOrDerived(SurfaceButtonPresenter::kClassId, close_class)) {
      auto close = SurfaceButtonPresenter::ptr{surface.chrome.close_button};
      close->locale = application.locale;
      close->label = application.label_close;
    } else if (IsClassOrDerived(ui::UiPushButtonPresenter::kClassId,
                                close_class)) {
      auto library_close = ui::UiPushButtonPresenter::ptr{
          surface.chrome.close_button};
      if (library_close->button) {
        library_close->button->label = application.label_close;
      }
      if (library_close->window_presenter) {
        library_close->window_presenter->locale = application.locale;
      }
    }
  }
  if (surface.chrome.language_button.is_valid()) {
    std::uint32_t const language_class =
        surface.chrome.language_button->GetClassId();
    if (IsClassOrDerived(SurfaceLanguageButtonPresenter::kClassId,
                         language_class)) {
      auto language = SurfaceLanguageButtonPresenter::ptr{
          surface.chrome.language_button};
      language->locale = application.locale;
      language->language_switch_label = application.language_switch_label;
    } else if (IsClassOrDerived(ui::UiPushButtonPresenter::kClassId,
                                language_class)) {
      auto library_language = ui::UiPushButtonPresenter::ptr{
          surface.chrome.language_button};
      if (library_language->button) {
        library_language->button->label = application.language_switch_label;
      }
      if (library_language->window_presenter) {
        library_language->window_presenter->locale = application.locale;
      }
    }
  }
}

}  // namespace apptraverse
