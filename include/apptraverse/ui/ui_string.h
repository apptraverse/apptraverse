#ifndef APPTRAVERSE_UI_UI_STRING_H_
#define APPTRAVERSE_UI_UI_STRING_H_

#include <string>

#include "aether-objects/obj/obj.h"

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

namespace apptraverse::ui {

// UTF-8 user-visible text stored in the model graph.
class UiString : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiString", UiString, ae::Obj, 0)

 protected:
  UiString() = default;

 public:
  explicit UiString(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(utf8))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, utf8);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, utf8);
  }

  std::string utf8;
};

}  // namespace apptraverse::ui

#endif  // APPTRAVERSE_UI_UI_STRING_H_
