#include "apptraverse/ui/ui_window_presenter.h"

#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui {

std::string UiWindowPresenter::ResolvePresentedWindowTitle(
    int language_index) const {
  title.Load();
  if (language_index < 0) {
    language_index = locale->language_index();
  }
  return title->Resolve(language_index);
}

}  // namespace apptraverse::ui
