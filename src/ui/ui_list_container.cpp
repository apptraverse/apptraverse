#include "apptraverse/ui/ui_list_container.h"

#include <algorithm>

namespace apptraverse::ui {

void UiListContainer::Apply(RemoveUiListRowEvent const& event) {
  rows.erase(
      std::remove_if(rows.begin(), rows.end(),
                     [&](UiListRow::ptr const& row) {
                       return row && row->obj_id == event.row_id;
                     }),
      rows.end());
}

}  // namespace apptraverse::ui
