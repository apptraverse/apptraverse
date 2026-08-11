#ifndef APPTRAVERSE_CHAT_PRESENTER_H_
#define APPTRAVERSE_CHAT_PRESENTER_H_

#include <cassert>
#include <string>
#include <utility>

#include "aether/obj/obj.h"

#include "model/chat.h"
#include "model/chat_events.h"
#include "model/client.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {

class ChatPresenter : public ae::Obj {
  APPTRAVERSE_OBJECT(ChatPresenter, ae::Obj, 1)

 protected:
  ChatPresenter() = default;

 public:
  explicit ChatPresenter(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(chat), AE_MMBR(local_client))

  Chat::ptr chat;
  Client::ptr local_client;

  void SubmitText(std::string text) {
    assert(chat.is_valid());
    assert(chat.is_loaded());
    assert(chat.domain() != nullptr);
    assert(local_client.is_valid());
    local_client.Load();
    assert(local_client.is_loaded());

    auto event = AddMessageEvent::ptr::Create(ae::CreateWith{*chat.domain()});
    event->author = local_client;
    event->text = std::move(text);
    chat->Commit(event);
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_PRESENTER_H_
