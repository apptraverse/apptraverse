#ifndef APPTRAVERSE_CHAT_H_
#define APPTRAVERSE_CHAT_H_

#include "aether/obj/obj.h"
#include "aether/obj/obj_ptr.h"

namespace apptraverse {

class ChatPresenter;

class Chat : public ae::Obj {
  AE_OBJECT(Chat, Obj, 0)

 protected:
  Chat() = default;

 public:
  explicit Chat(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(presenter))

  ae::ObjPtr<ChatPresenter> presenter;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_H_
