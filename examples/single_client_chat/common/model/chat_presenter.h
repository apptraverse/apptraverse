#ifndef APPTRAVERSE_CHAT_PRESENTER_H_
#define APPTRAVERSE_CHAT_PRESENTER_H_

#include "aether/obj/obj.h"

#include "model/chat.h"
#include "model/client.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {

class ChatPresenter : public ae::Obj {
  APPTRAVERSE_OBJECT(ChatPresenter, ae::Obj, 1)

 protected:
  ChatPresenter() = default;

 public:
  explicit ChatPresenter(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(chat), AE_MMBR(local_client))

  LocalPtr<Chat> chat;
  LocalPtr<Client> local_client;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_PRESENTER_H_
