#include "apptraverse/ui/ui_localization_string.h"

#include <cstdlib>
#include <optional>
#include <vector>

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
  // Compaction moves edits into the immutable base without writing USER.
  // An unloaded live slot can therefore be older on disk than the current
  // base. Restore its value from that base before replaying the retained tail,
  // keeping the original live slot ID and its presentation loading policy.
  if (localized.base.is_valid()) {
    ae::Ptr<ae::Obj> const historical_object = localized.base.Load();
    if (!historical_object) {
      std::abort();
    }
    auto const& historical =
        static_cast<UiLocalizationString const&>(*historical_object);
    if (slot_index < historical.strings.size() &&
        historical.strings[slot_index].is_valid()) {
      auto historical_slot = historical.strings[slot_index].Load();
      if (!historical_slot) {
        std::abort();
      }
      slot->utf8 = historical_slot->utf8;
    }
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

UiLocalizationString::CompactionOperationScratch& AsCompactionScratch(
    void* compaction_scratch) {
  return *static_cast<UiLocalizationString::CompactionOperationScratch*>(
      compaction_scratch);
}

UiLocalizationString::RebuildOperationScratch& AsRebuildScratch(
    void* rebuild_scratch) {
  return *static_cast<UiLocalizationString::RebuildOperationScratch*>(
      rebuild_scratch);
}

}  // namespace

void UiLocalizationString::FoldCollapsedJournalPrefixIntoReachableStorage(
    std::size_t collapsed_count, void* compaction_scratch) {
  if (compaction_scratch == nullptr) {
    std::abort();
  }
  auto& scratch = AsCompactionScratch(compaction_scratch);
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
    if (!keep_loaded) {
      scratch.folded_slots.push_back(slot);
    }
  }
}

void UiLocalizationString::ReleaseFoldCaptureMaterialization(
    void* compaction_scratch) {
  if (compaction_scratch == nullptr) {
    std::abort();
  }
  auto& scratch = AsCompactionScratch(compaction_scratch);
  for (std::size_t slot : scratch.folded_slots) {
    if (slot < strings.size() && strings[slot].is_loaded()) {
      strings[slot].Reset();
    }
  }
  scratch.folded_slots.clear();
}

void UiLocalizationString::StashLocalPersistentAcrossRebuild(
    void* rebuild_scratch) {
  if (rebuild_scratch == nullptr) {
    std::abort();
  }
  auto& scratch = AsRebuildScratch(rebuild_scratch);
  scratch.strings = strings;
}

void UiLocalizationString::RestoreLocalPersistentAcrossRebuild(
    void* rebuild_scratch) {
  if (rebuild_scratch == nullptr) {
    std::abort();
  }
  auto& scratch = AsRebuildScratch(rebuild_scratch);
  if (!scratch.strings.empty()) {
    strings = std::move(scratch.strings);
    scratch.strings.clear();
  }
}

void UiLocalizationString::PrepareReachableMutableLeavesForHistoricalCapture() {
  for (std::size_t slot = 0; slot < strings.size(); ++slot) {
    if (!strings[slot].is_valid()) {
      continue;
    }
    if (!strings[slot].is_loaded()) {
      ReplaySlotUtf8FromJournal(*this, slot, nullptr);
    }
  }
}

void UiLocalizationString::StashLiveReachableLayersBeforeHistoricalCapture(
    void* compaction_scratch) {
  if (compaction_scratch == nullptr) {
    std::abort();
  }
  auto& scratch = AsCompactionScratch(compaction_scratch);
  scratch.live_slot_utf8.assign(strings.size(), std::nullopt);
  for (std::size_t slot = 0; slot < strings.size(); ++slot) {
    if (!strings[slot].is_valid()) {
      continue;
    }
    if (strings[slot].is_loaded()) {
      scratch.live_slot_utf8[slot] = strings[slot]->utf8;
      continue;
    }
    bool const loaded_here = !strings[slot].is_loaded();
    // Capture will materialize every mutable language. Remember all originally
    // lazy slots, not just the slots affected by the compacted event prefix.
    if (loaded_here) {
      scratch.folded_slots.push_back(slot);
    }
    if (loaded_here) {
      strings[slot].Load();
    }
    ReplaySlotUtf8FromJournal(*this, slot, nullptr);
    scratch.live_slot_utf8[slot] = strings[slot]->utf8;
    if (loaded_here) {
      strings[slot].Reset();
    }
  }
}

void UiLocalizationString::RestoreLiveReachableLayersAfterHistoricalCapture(
    void* compaction_scratch) {
  if (compaction_scratch == nullptr) {
    std::abort();
  }
  auto& scratch = AsCompactionScratch(compaction_scratch);
  for (std::size_t slot = 0; slot < scratch.live_slot_utf8.size(); ++slot) {
    if (!scratch.live_slot_utf8[slot].has_value() || !strings[slot].is_valid()) {
      continue;
    }
    bool const was_loaded = strings[slot].is_loaded();
    if (!strings[slot].is_loaded()) {
      strings[slot].Load();
    }
    strings[slot]->utf8 = *scratch.live_slot_utf8[slot];
    if (!was_loaded) {
      strings[slot].Reset();
    }
  }
  scratch.live_slot_utf8.clear();
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
