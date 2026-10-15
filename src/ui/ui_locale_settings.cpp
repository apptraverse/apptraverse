#include "apptraverse/ui/ui_locale_settings.h"

namespace apptraverse::ui {

void UiLocaleSettings::Apply(SetUiLanguageIndexEvent const& event) {
  if (language_index_ == event.language_index) {
    return;
  }
  language_index_ = event.language_index;
  NoteMaterializedChange();
}

}  // namespace apptraverse::ui
