#include "apptraverse/ui/ui_localization_string.h"

#include <cstdlib>

namespace apptraverse::ui {

void UiLocalizationString::SyncSessionSlotCount() {
  if (slot_session_utf8_.size() < strings.size()) {
    slot_session_utf8_.resize(strings.size());
  }
}

void UiLocalizationString::CaptureSlotToSession(std::size_t index) {
  SyncSessionSlotCount();
  if (index >= strings.size() || !strings[index].is_loaded()) {
    return;
  }
  slot_session_utf8_[index] = strings[index]->utf8;
}

void UiLocalizationString::ApplySlotUtf8FromJournal(std::size_t index) {
  SyncSessionSlotCount();
  for (auto const& record : journal) {
    if (!record.event) {
      continue;
    }
    if (record.event->GetClassId() != SetUiLocalizationSlotUtf8Event::kClassId) {
      continue;
    }
    auto const& event =
        static_cast<SetUiLocalizationSlotUtf8Event const&>(*record.event);
    if (static_cast<std::size_t>(event.slot_index) != index) {
      continue;
    }
    slot_session_utf8_[index] = event.utf8;
  }
}

void UiLocalizationString::ApplySessionToSlot(std::size_t index) {
  SyncSessionSlotCount();
  if (index >= strings.size()) {
    return;
  }
  if (!slot_session_utf8_[index]) {
    ApplySlotUtf8FromJournal(index);
  }
  if (!slot_session_utf8_[index]) {
    return;
  }
  if (!strings[index].is_loaded()) {
    strings[index].Load();
  }
  strings[index]->utf8 = *slot_session_utf8_[index];
}

void UiLocalizationString::MaterializeLanguage(int language_index) {
  if (language_index < 0 ||
      static_cast<std::size_t>(language_index) >= strings.size()) {
    std::abort();
  }
  auto const slot = static_cast<std::size_t>(language_index);

  bool changed = false;
  if (!strings[slot].is_loaded()) {
    strings[slot].Load();
    ApplySessionToSlot(slot);
    changed = true;
  }
  for (std::size_t i = 0; i < strings.size(); ++i) {
    if (i == slot) {
      continue;
    }
    if (strings[i].is_loaded()) {
      CaptureSlotToSession(i);
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
  auto const index = static_cast<std::size_t>(language_index);
  auto const& entry = strings[index];
  if (entry) {
    return entry->utf8;
  }
  if (index < slot_session_utf8_.size() && slot_session_utf8_[index]) {
    return *slot_session_utf8_[index];
  }
  for (auto const& record : journal) {
    if (!record.event) {
      continue;
    }
    if (record.event->GetClassId() != SetUiLocalizationSlotUtf8Event::kClassId) {
      continue;
    }
    auto const& event =
        static_cast<SetUiLocalizationSlotUtf8Event const&>(*record.event);
    if (static_cast<std::size_t>(event.slot_index) != index) {
      continue;
    }
    return event.utf8;
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
  if (strings[index].is_loaded()) {
    if (strings[index]->utf8 == event.utf8) {
      return;
    }
    strings[index]->utf8 = event.utf8;
    SyncSessionSlotCount();
    slot_session_utf8_[index] = event.utf8;
    NoteMaterializedChange();
    return;
  }
  auto loaded = strings[index].Load();
  if (loaded->utf8 == event.utf8) {
    strings[index].Reset();
    return;
  }
  loaded->utf8 = event.utf8;
  SyncSessionSlotCount();
  slot_session_utf8_[index] = event.utf8;
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
