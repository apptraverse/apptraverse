#ifndef APPTRAVERSE_UI_UI_LOCALIZATION_STRING_H_
#define APPTRAVERSE_UI_UI_LOCALIZATION_STRING_H_

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

#include "apptraverse/ui/ui_string.h"

namespace apptraverse::ui {

class SetUiLocalizationSlotUtf8Event;

enum class UiLanguage : int {
  kEnglish = 0,
  kRussian = 1,
  kCount = 2,
};

inline int UiLanguageCount() {
  return static_cast<int>(UiLanguage::kCount);
}

class UiLocalizationString : public NodeFor<UiLocalizationString> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiLocalizationString",
                           UiLocalizationString, Node, 0)

 protected:
  UiLocalizationString() = default;

 public:
  explicit UiLocalizationString(ae::ObjProp prop) : NodeFor{prop} {}

  struct RebuildOperationScratch {
    std::vector<UiString::ptr> strings;
  };

  struct CompactionOperationScratch {
    std::vector<std::size_t> folded_slots;
    std::vector<std::optional<std::string>> live_slot_utf8;
  };

  AE_OBJECT_REFLECT(AE_MMBR(strings))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, strings);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, strings);
  }

  std::vector<UiString::ptr> strings;

  std::string Resolve(int language_index) const;

  void MaterializeLanguage(int language_index);

  void Apply(SetUiLocalizationSlotUtf8Event const& event);

  void FoldCollapsedJournalPrefixIntoReachableStorage(
      std::size_t collapsed_count, void* compaction_scratch) override;

  void ReleaseFoldCaptureMaterialization(void* compaction_scratch) override;

  void StashLocalPersistentAcrossRebuild(void* rebuild_scratch) override;

  void RestoreLocalPersistentAcrossRebuild(void* rebuild_scratch) override;

  void StashLiveReachableLayersBeforeHistoricalCapture(
      void* compaction_scratch) override;

  void RestoreLiveReachableLayersAfterHistoricalCapture(
      void* compaction_scratch) override;

  void PrepareReachableMutableLeavesForHistoricalCapture() override;
};

class SetUiLocalizationSlotUtf8Event
    : public EventFor<UiLocalizationString, SetUiLocalizationSlotUtf8Event> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::SetUiLocalizationSlotUtf8Event",
                           SetUiLocalizationSlotUtf8Event, Event, 0)

 protected:
  SetUiLocalizationSlotUtf8Event() = default;

 public:
  explicit SetUiLocalizationSlotUtf8Event(ae::ObjProp prop) : EventFor{prop}, slot_index{0} {}

  AE_OBJECT_REFLECT(AE_MMBR(slot_index), AE_MMBR(utf8))

  int slot_index;
  std::string utf8;
};

std::string ResolveLocalization(UiLocalizationString const* localization,
                                int language_index);

}  // namespace apptraverse::ui

#endif  // APPTRAVERSE_UI_UI_LOCALIZATION_STRING_H_
