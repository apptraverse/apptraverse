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

SetUiLocalizationSlotUtf8Event const* StopBeforeJournalIndex(
    std::vector<EventRecord> const& journal, std::size_t index) {
  if (index >= journal.size()) {
    return nullptr;
  }
  auto const& record = journal[index];
  if (!record.event.is_valid() || !record.event.is_loaded()) {
    return nullptr;
  }
  if (record.event->GetClassId() != SetUiLocalizationSlotUtf8Event::kClassId) {
    return nullptr;
  }
  return static_cast<SetUiLocalizationSlotUtf8Event const*>(&*record.event);
}

}  // namespace

void UiLocalizationString::FoldCollapsedJournalPrefixIntoReachableStorage(
    std::size_t collapsed_count) {
  if (collapsed_count == 0 || journal.empty()) {
    return;
  }
  collapsed_count = std::min(collapsed_count, journal.size());
  SetUiLocalizationSlotUtf8Event const* const stop_before =
      StopBeforeJournalIndex(journal, collapsed_count);

  for (std::size_t slot = 0; slot < strings.size(); ++slot) {
    bool affects_slot = false;
    for (std::size_t i = 0; i < collapsed_count; ++i) {
      auto const& record = journal[i];
      if (!record.event.is_valid() || !record.event.is_loaded()) {
        continue;
      }
      if (record.event->GetClassId() != SetUiLocalizationSlotUtf8Event::kClassId) {
        continue;
      }
      auto const& event =
          static_cast<SetUiLocalizationSlotUtf8Event const&>(*record.event);
      if (static_cast<std::size_t>(event.slot_index) == slot) {
        affects_slot = true;
        break;
      }
    }
    if (!affects_slot) {
      continue;
    }
    bool const keep_loaded = strings[slot].is_loaded();
    if (!strings[slot].is_loaded()) {
      strings[slot].Load();
    }
    ReplaySlotUtf8FromJournal(*this, slot, stop_before);
    strings[slot].Save();
    if (!keep_loaded) {
      strings[slot].Reset();
    }
  }
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
