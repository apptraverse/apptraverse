#ifndef APPTRAVERSE_APPLICATION_IDS_H_
#define APPTRAVERSE_APPLICATION_IDS_H_

#include "aether/obj/obj_id.h"

namespace apptraverse {

// Fixed AppTraverse application ObjIds. Aether system objects use low IDs;
// application graph IDs start at 100000 and must stay unique and named.
enum class ApplicationObjId : ae::ObjId::Type {
  Application = 100000,
  Window = 100001,
  WindowPresenter = 100002,
  ChatBase = 100003,
  Chat = 100004,
  ChatPresenter = 100005,
  Alice = 100006,
  JoinClientEvent = 100007,
};

constexpr ae::ObjId::Type ToObjId(ApplicationObjId id) {
  return static_cast<ae::ObjId::Type>(id);
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_APPLICATION_IDS_H_
