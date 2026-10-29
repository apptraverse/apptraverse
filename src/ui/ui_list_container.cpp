#include "apptraverse/ui/ui_list_container.h"

#include <algorithm>

namespace apptraverse::ui {

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
  rows.erase(
      std::remove_if(rows.begin(), rows.end(),
                     [&](UiListRow::ptr const& row) {
                       return row && row->obj_id == event.row_id;
                     }),
      rows.end());
}

}  // namespace apptraverse::ui
