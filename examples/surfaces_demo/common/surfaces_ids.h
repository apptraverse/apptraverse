#ifndef APPTRAVERSE_SURFACES_IDS_H_
#define APPTRAVERSE_SURFACES_IDS_H_

#include <cstdint>

#include "aether-objects/obj/obj_id.h"

namespace apptraverse::surfaces_demo {

enum class ObjId : ae::ObjId::Type {
  Application = 1,
  Surfaces = 2,
  Surface1 = 3,
  Surface1Presenter = 4,
};

inline constexpr ae::ObjId::Type ToObjId(ObjId id) {
  return static_cast<ae::ObjId::Type>(id);
}

}  // namespace apptraverse::surfaces_demo

#endif  // APPTRAVERSE_SURFACES_IDS_H_
