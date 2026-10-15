#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui {

std::string UiLocalizationString::Resolve(int language_index) const {
  return ResolveLocalization(this, language_index);
}

void UiLocalizationString::Apply(SetUiLocalizationSlotUtf8Event const& event) {
  if (event.slot_index < 0) {
    return;
  }
  auto const index = static_cast<std::size_t>(event.slot_index);
  if (index >= strings.size() || !strings[index]) {
    return;
  }
  if (strings[index]->utf8 == event.utf8) {
    return;
  }
  strings[index]->utf8 = event.utf8;
  NoteMaterializedChange();
}

std::string ResolveLocalization(UiLocalizationString const* localization,
                                int language_index) {
  if (localization == nullptr || localization->strings.empty()) {
    return {};
  }
  if (language_index < 0 ||
      static_cast<std::size_t>(language_index) >=
          localization->strings.size()) {
    if (localization->strings.front()) {
      return localization->strings.front()->utf8;
    }
    return {};
  }
  auto const& entry = localization->strings[static_cast<std::size_t>(
      language_index)];
  if (entry) {
    return entry->utf8;
  }
  if (localization->strings.front()) {
    return localization->strings.front()->utf8;
  }
  return {};
}

}  // namespace apptraverse::ui
