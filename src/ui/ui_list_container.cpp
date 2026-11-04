#include "apptraverse/ui/ui_list_container.h"

#include <algorithm>

namespace apptraverse::ui {

std::string ResolvePresentedListRowPrimaryText(UiListRow::ptr const& row,
                                               int language_index) {
  row.Load();
  return row->primary_text->Resolve(language_index);
}

std::vector<std::string> ResolvePresentedListRowPrimaryTexts(
    UiListContainer const& container, int language_index) {
  std::vector<std::string> labels;
  labels.reserve(container.rows.size());
  for (auto const& row : container.rows) {
    labels.push_back(ResolvePresentedListRowPrimaryText(row, language_index));
  }
  return labels;
}

void UiListContainer::SubmitListRowActivatedFromUi(std::size_t row_index) {
  HandleListRowActivated(row_index);
}

void UiListContainer::SubmitListRowSelectionChangedFromUi(
    std::size_t row_index) {
  HandleListRowSelectionChanged(row_index);
}

void UiListContainer::HandleListRowActivated(std::size_t row_index) {
  static_cast<void>(row_index);
}

void UiListContainer::HandleListRowSelectionChanged(std::size_t row_index) {
  std::int32_t const next = static_cast<std::int32_t>(row_index);
  if (selection_index == next) {
    return;
  }
  auto event = SetUiListSelectionEvent::ptr::Create(ae::CreateWith{*domain});
  event->selection_index = next;
  Commit(std::move(event));
}

void UiListContainer::Apply(SetUiListSelectionEvent const& event) {
  if (selection_index == event.selection_index) {
    return;
  }
  selection_index = event.selection_index;
  NoteMaterializedChange();
}

void UiListContainer::Apply(RemoveUiListRowEvent const& event) {
  // Row identity is the reference id; an unloaded row is removed without
  // materializing its body.
  auto const removed_begin =
      std::remove_if(rows.begin(), rows.end(), [&](UiListRow::ptr const& row) {
        return row.id() == event.row_id;
      });
  if (removed_begin == rows.end()) {
    return;
  }
  rows.erase(removed_begin, rows.end());
  NoteMaterializedChange();
}

}  // namespace apptraverse::ui
