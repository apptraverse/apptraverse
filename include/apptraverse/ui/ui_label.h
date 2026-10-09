#ifndef APPTRAVERSE_UI_UI_LABEL_H_
#define APPTRAVERSE_UI_UI_LABEL_H_

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui {

class UiLabel : public NodeFor<UiLabel> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiLabel", UiLabel, Node, 0)

 protected:
  UiLabel() = default;

 public:
  explicit UiLabel(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(text))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, text);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, text);
  }

  ae::ObjPtr<UiLocalizationString> text;
};

}  // namespace apptraverse::ui

#endif  // APPTRAVERSE_UI_UI_LABEL_H_
