#include "apptraverse/ui/ui_localization_string.h"

#include <cstdlib>

namespace apptraverse::ui {
namespace {

void ReplaySlotUtf8FromJournal(UiLocalizationString& localized,
                               std::size_t slot_index,
                               SetUiLocalizationSlotUtf8Event const* stop_before) {
  if (slot_index >= localized.strings.size()) {
    return;
  }
  auto& slot = localized.strings[slot_index];
  if (!slot.is_loaded()) {
    slot.Load();
  }
  for (auto const& record : localized.journal) {
    if (stop_before != nullptr && record.event.is_valid() &&
        record.event.is_loaded() &&
        &*record.event == stop_before) {
      break;
    }
    if (!record.event.is_valid() || !record.event.is_loaded()) {
      continue;
    }
    if (record.event->GetClassId() != SetUiLocalizationSlotUtf8Event::kClassId) {
      continue;
    }
    auto const& event =
        static_cast<SetUiLocalizationSlotUtf8Event const&>(*record.event);
    if (static_cast<std::size_t>(event.slot_index) != slot_index) {
      continue;
    }
    slot->utf8 = event.utf8;
  }
}

}  // namespace

void UiLocalizationString::MaterializeLanguage(int language_index) {
  if (language_index < 0 ||
      static_cast<std::size_t>(language_index) >= strings.size()) {
    std::abort();
  }
  auto const slot = static_cast<std::size_t>(language_index);

  bool changed = false;
  if (!strings[slot].is_loaded()) {
    strings[slot].Load();
    ReplaySlotUtf8FromJournal(*this, slot, nullptr);
    changed = true;
  }
  for (std::size_t i = 0; i < strings.size(); ++i) {
    if (i == slot) {
      continue;
    }
    if (strings[i].is_loaded()) {
      strings[i].Reset();
      changed = true;
    }
  }
  if (changed) {
    NoteMaterializedChange();
  }
}

std::string UiLocalizationString::Resolve(int language_index) const {
  if (strings.empty()) {
    return {};
  }
  if (language_index < 0 ||
      static_cast<std::size_t>(language_index) >= strings.size()) {
    std::abort();
  }
  auto const& entry = strings[static_cast<std::size_t>(language_index)];
  if (entry) {
    return entry->utf8;
  }
  return {};
}

void UiLocalizationString::Apply(SetUiLocalizationSlotUtf8Event const& event) {
  if (event.slot_index < 0) {
    return;
  }
  auto const index = static_cast<std::size_t>(event.slot_index);
  if (index >= strings.size() || !strings[index].is_valid()) {
    return;
  }
  bool const was_loaded = strings[index].is_loaded();
  if (was_loaded) {
    if (strings[index]->utf8 == event.utf8) {
      return;
    }
    strings[index]->utf8 = event.utf8;
    NoteMaterializedChange();
    return;
  }
  ReplaySlotUtf8FromJournal(*this, index, &event);
  if (strings[index]->utf8 == event.utf8) {
    strings[index].Reset();
    return;
  }
  strings[index]->utf8 = event.utf8;
  strings[index].Reset();
  NoteMaterializedChange();
}

std::string ResolveLocalization(UiLocalizationString const* localization,
                                int language_index) {
  if (localization == nullptr) {
    return {};
  }
  return localization->Resolve(language_index);
}

}  // namespace apptraverse::ui
