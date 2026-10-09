#include "apptraverse/ui/ui_locale_settings.h"

namespace apptraverse::ui {

void UiLocaleSettings::Apply(SetUiLanguageIndexEvent const& event) {
  language_index_ = event.language_index;
}

}  // namespace apptraverse::ui
