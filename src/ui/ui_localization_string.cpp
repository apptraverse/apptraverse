#include "apptraverse/ui/ui_localization_string.h"

#include <cstdlib>

namespace apptraverse::ui {

void UiLocalizationString::MaterializeLanguage(int language_index) {
  if (language_index < 0 ||
      static_cast<std::size_t>(language_index) >= strings.size()) {
    std::abort();
  }
  auto const slot = static_cast<std::size_t>(language_index);

  bool changed = false;
  if (!strings[slot].is_loaded()) {
    strings[slot].Load();
    changed = true;
  }
  for (std::size_t i = 0; i < strings.size(); ++i) {
    if (i == slot) {
      continue;
    }
    if (strings[i].is_loaded()) {
      strings[i].Save();
      strings[i].Reset();
      changed = true;
    }
  }
  if (changed) {
    NoteMaterializedChange();
  }
}

std::string UiLocalizationString::Resolve(int language_index) const {
  return ResolveLocalization(this, language_index);
}

void UiLocalizationString::Apply(SetUiLocalizationSlotUtf8Event const& event) {
  if (event.slot_index < 0) {
    return;
  }
  auto const index = static_cast<std::size_t>(event.slot_index);
  if (index >= strings.size() || !strings[index].is_valid()) {
    return;
  }
  if (strings[index].is_loaded()) {
    if (strings[index]->utf8 == event.utf8) {
      return;
    }
    strings[index]->utf8 = event.utf8;
    NoteMaterializedChange();
    return;
  }
  auto loaded = strings[index].Load();
  if (loaded->utf8 == event.utf8) {
    strings[index].Reset();
    return;
  }
  loaded->utf8 = event.utf8;
  strings[index].Save();
  strings[index].Reset();
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
    std::abort();
  }
  auto const& entry = localization->strings[static_cast<std::size_t>(
      language_index)];
  if (entry) {
    return entry->utf8;
  }
  return {};
}

}  // namespace apptraverse::ui
