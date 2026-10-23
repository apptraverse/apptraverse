#ifndef APPTRAVERSE_WINDOW_CATALOG_LOCALIZATION_H_
#define APPTRAVERSE_WINDOW_CATALOG_LOCALIZATION_H_

#include "window_catalog_model.h"

namespace apptraverse {

void InstallWindowCatalogLocalization(WindowCatalog& catalog,
                                      ae::Domain& domain);

std::string FormatCatalogWindowTitle(ui::UiLocalizationString const& prefix,
                                     int language_index,
                                     std::uint32_t number);

void MaterializeWindowHolderTitle(WindowHolder& holder, int language_index,
                                  ui::UiLocalizationString const& prefix);

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_LOCALIZATION_H_
