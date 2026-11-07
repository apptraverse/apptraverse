#ifndef APPTRAVERSE_WINDOW_CATALOG_SUPPLY_CONSTANT_IDS_H_
#define APPTRAVERSE_WINDOW_CATALOG_SUPPLY_CONSTANT_IDS_H_

#include <vector>

#include "aether-objects/obj/obj_id.h"

namespace apptraverse {

class WindowCatalog;

std::vector<ae::ObjId> CollectWindowCatalogSupplyConstantIds(
    WindowCatalog const& catalog);

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_SUPPLY_CONSTANT_IDS_H_
