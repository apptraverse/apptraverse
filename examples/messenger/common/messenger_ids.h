#ifndef APPTRAVERSE_MESSENGER_IDS_H_
#define APPTRAVERSE_MESSENGER_IDS_H_

#include <cstdint>

#include "aether-objects/obj/obj_id.h"

namespace apptraverse::messenger {

enum class ObjId : ae::ObjId::Type {
  Application = 1,
  Surfaces = 2,
  Surface1 = 3,
  Surface1Presenter = 4,
  Dialog = 5,
};

inline constexpr ae::ObjId::Type ToObjId(ObjId id) {
  return static_cast<ae::ObjId::Type>(id);
}

}  // namespace apptraverse::messenger

#endif  // APPTRAVERSE_MESSENGER_IDS_H_
