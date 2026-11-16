#include "window_catalog_supply_constant_ids.h"

#include "window_catalog_model.h"

#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse {
namespace {

void AppendLocalizationStrings(ui::UiLocalizationString const& localization,
                               std::vector<ae::ObjId>& out) {
  out.push_back(localization.obj_id);
  for (auto const& entry : localization.strings) {
    out.push_back(entry.id());
  }
}

void AppendLocalizationRef(ui::UiLocalizationString::ptr const& localized,
                           std::vector<ae::ObjId>& out) {
  localized.Load();
  AppendLocalizationStrings(*localized, out);
}

}  // namespace

std::vector<ae::ObjId> CollectWindowCatalogSupplyConstantIds(
    WindowCatalog const& catalog) {
  std::vector<ae::ObjId> ids;
  auto const& texts = catalog.persisted.texts;
  AppendLocalizationRef(texts.label_add, ids);
  AppendLocalizationRef(texts.label_open, ids);
  AppendLocalizationRef(texts.label_delete, ids);
  AppendLocalizationRef(texts.label_language, ids);
  AppendLocalizationRef(texts.window_title_prefix, ids);
  AppendLocalizationRef(texts.label_close, ids);
  AppendLocalizationRef(texts.list_row_title_format, ids);
  AppendLocalizationRef(texts.list_row_title_closed_suffix, ids);
  catalog.persisted.main_window.Load();
  auto const& main_title =
      static_cast<ui::UiPresentedWindow const&>(*catalog.persisted.main_window)
          .presenter->title;
  AppendLocalizationRef(main_title, ids);
  return ids;
}

}  // namespace apptraverse
