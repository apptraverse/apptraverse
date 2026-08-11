#ifndef APPTRAVERSE_WINDOW_H_
#define APPTRAVERSE_WINDOW_H_

#include "aether/obj/obj.h"

#include "apptraverse/node.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {

class WindowPresenter;
class Chat;
class WindowChangedEvent;

// Platform-neutral Window Node. Concrete platform windows inherit through
// NodeFor<Concrete, Window> and implement Apply(WindowChangedEvent).
class Window : public Node {
  APPTRAVERSE_OBJECT(Window, Node, 0)

 protected:
  Window() = default;

 public:
  explicit Window(ae::ObjProp prop) : Node{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(presenter), AE_MMBR(chat))

  LocalPtr<WindowPresenter> presenter;
  SharedPtr<Chat> chat;

  virtual void Apply(WindowChangedEvent const& event);
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_H_
