#ifndef APPTRAVERSE_UI_PRESENTED_WINDOW_COMPOSITION_H_
#define APPTRAVERSE_UI_PRESENTED_WINDOW_COMPOSITION_H_

#include <type_traits>

#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_presented_window.h"
#include "apptraverse/ui/ui_window_presenter.h"

namespace apptraverse::ui {

template <typename WindowT, typename PresenterT>
void ComposePresentedWindow(ae::ObjPtr<WindowT> const& window,
                              ae::ObjPtr<PresenterT> const& presenter,
                              ae::ObjPtr<UiLocalizationString> const& title,
                              ae::ObjPtr<UiLocaleSettings> const& locale) {
  static_assert(std::is_base_of_v<UiPresentedWindow, WindowT>);
  static_assert(std::is_base_of_v<UiWindowPresenter, PresenterT>);
  window->presenter = presenter;
  presenter->window = window;
  presenter->title = title;
  presenter->locale = locale;
}

}  // namespace apptraverse::ui

#endif  // APPTRAVERSE_UI_PRESENTED_WINDOW_COMPOSITION_H_
