#ifndef APPTRAVERSE_CHAT_PRESENTER_H_
#define APPTRAVERSE_CHAT_PRESENTER_H_

#include "aether/obj/obj.h"

#include "apptraverse/chat.h"

namespace apptraverse {

class WindowPresenter;

class ChatPresenter : public ae::Obj {
  AE_OBJECT(ChatPresenter, Obj, 0)

 protected:
  ChatPresenter() = default;

 public:
  explicit ChatPresenter(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(chat), AE_MMBR(window_presenter))

  Chat::ptr chat;
  ae::ObjPtr<WindowPresenter> window_presenter;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_PRESENTER_H_
