#ifndef APPTRAVERSE_CHAT_PRESENTER_H_
#define APPTRAVERSE_CHAT_PRESENTER_H_

#include <cassert>
#include <string>
#include <utility>

#include "aether/obj/obj.h"

#include "apptraverse/chat.h"
#include "apptraverse/chat_events.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {

class ChatPresenter : public ae::Obj {
  APPTRAVERSE_OBJECT(ChatPresenter, ae::Obj, 0)

 protected:
  ChatPresenter() = default;

 public:
  explicit ChatPresenter(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(chat))

  Chat::ptr chat;

  void SubmitText(std::string text) {
    assert(chat.is_valid());
    assert(chat.is_loaded());
    assert(chat.domain() != nullptr);

    auto author = chat->FindJoinedClient();
    assert(author.is_valid());
    author.Load();
    assert(author.is_loaded());

    auto event = AddMessageEvent::ptr::Create(ae::CreateWith{*chat.domain()});
    event->author = author;
    event->text = std::move(text);
    chat->Commit(event);
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_PRESENTER_H_
