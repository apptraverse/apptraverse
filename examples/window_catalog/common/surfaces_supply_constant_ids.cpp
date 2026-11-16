#include "surfaces_supply_constant_ids.h"

#include "apptraverse/surfaces/surfaces_model.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse {
namespace {

void AppendLocalizationStrings(ui::UiLocalizationString const* localization,
                               std::vector<ae::ObjId>& out) {
  if (localization == nullptr) {
    return;
  }
  for (auto const& entry : localization->strings) {
    if (entry.is_valid()) {
      out.push_back(entry.id());
    }
  }
}

}  // namespace

std::vector<ae::ObjId> CollectSurfacesDemoSupplyConstantIds(
    Application const& application) {
  std::vector<ae::ObjId> ids;
  if (application.label_add) {
    AppendLocalizationStrings(application.label_add.operator->(), ids);
  }
  if (application.label_close) {
    AppendLocalizationStrings(application.label_close.operator->(), ids);
  }
  if (application.window_title_prefix) {
    AppendLocalizationStrings(application.window_title_prefix.operator->(), ids);
  }
  if (application.language_switch_label) {
    AppendLocalizationStrings(application.language_switch_label.operator->(), ids);
  }
  return ids;
}

}  // namespace apptraverse
