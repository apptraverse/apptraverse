#ifndef APPTRAVERSE_SURFACES_SUPPLY_CONSTANT_IDS_H_
#define APPTRAVERSE_SURFACES_SUPPLY_CONSTANT_IDS_H_

#include <vector>

#include "aether-objects/obj/obj_id.h"

namespace apptraverse {

class Application;

// Distill-time supply graph roles (demo localization UiString bodies).
std::vector<ae::ObjId> CollectSurfacesDemoSupplyConstantIds(
    Application const& application);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_SUPPLY_CONSTANT_IDS_H_
