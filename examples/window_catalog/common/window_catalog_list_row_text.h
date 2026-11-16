#ifndef APPTRAVERSE_WINDOW_CATALOG_LIST_ROW_TEXT_H_
#define APPTRAVERSE_WINDOW_CATALOG_LIST_ROW_TEXT_H_

#include <cstdint>
#include <string>
#include <vector>

#include "window_catalog_model.h"

#include "apptraverse/ui/ui_list_container.h"

namespace apptraverse {

bool IsWindowCatalogHolderWindowOpen(WindowHolder const& holder);

std::string FormatWindowCatalogListRowText(WindowCatalogTexts const& texts,
                                           int language_index,
                                           std::uint32_t window_number,
                                           bool window_open);

std::string ResolveWindowCatalogHolderListRowText(WindowHolder const& holder,
                                                WindowCatalog const& catalog,
                                                int language_index);

std::string ResolveWindowCatalogListRowPrimaryText(
    ui::UiListRow::ptr const& row, WindowCatalog const& catalog,
    int language_index);

std::vector<std::string> ResolveWindowCatalogListRowPrimaryTexts(
    ui::UiListContainer const& list, WindowCatalog const& catalog,
    int language_index);

std::string ResolveWindowCatalogChildWindowTitle(WindowCatalog const& catalog,
                                                 ae::ObjId window_obj_id,
                                                 int language_index);

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_LIST_ROW_TEXT_H_
