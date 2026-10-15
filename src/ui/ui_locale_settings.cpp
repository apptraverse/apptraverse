#include "apptraverse/ui/ui_locale_settings.h"

namespace apptraverse::ui {

void UiLocaleSettings::Apply(SetUiLanguageIndexEvent const& event) {
  if (language_index_ == event.language_index) {
    return;
  }
  language_index_ = event.language_index;
  NoteMaterializedChange();
}

void UiLocaleSettings::SubmitLanguageIndexFromUi(int language_index) {
  if (language_index_ == language_index) {
    return;
  }
  auto event = SetUiLanguageIndexEvent::ptr::Create(ae::CreateWith{*domain});
  event->language_index = language_index;
  Commit(std::move(event));
}

}  // namespace apptraverse::ui
