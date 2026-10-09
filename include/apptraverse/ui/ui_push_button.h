#ifndef APPTRAVERSE_UI_UI_PUSH_BUTTON_H_
#define APPTRAVERSE_UI_UI_PUSH_BUTTON_H_

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui {

class UiPushButtonClickedEvent;
class SetUiPushButtonEnabledEvent;

class UiPushButton : public NodeFor<UiPushButton> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiPushButton", UiPushButton, Node,
                           0)

 protected:
  UiPushButton() = default;

 public:
  explicit UiPushButton(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label), AE_MMBR(enabled_))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, label, enabled_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, label, enabled_);
  }

  ae::ObjPtr<UiLocalizationString> label;
  bool enabled() const { return enabled_; }

  void NotifyClicked();
  void BootstrapSetEnabled(bool enabled) { enabled_ = enabled; }
  void Apply(UiPushButtonClickedEvent const& event);
  void Apply(SetUiPushButtonEnabledEvent const& event);

 private:
  bool enabled_{true};
};

class SetUiPushButtonEnabledEvent
    : public EventFor<UiPushButton, SetUiPushButtonEnabledEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::SetUiPushButtonEnabledEvent",
                           SetUiPushButtonEnabledEvent, Event, 0)

 protected:
  SetUiPushButtonEnabledEvent() = default;

 public:
  explicit SetUiPushButtonEnabledEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(enabled))

  bool enabled{true};
};

class UiPushButtonClickedEvent
    : public EventFor<UiPushButton, UiPushButtonClickedEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiPushButtonClickedEvent",
                           UiPushButtonClickedEvent, Event, 0)

 protected:
  UiPushButtonClickedEvent() = default;

 public:
  explicit UiPushButtonClickedEvent(ae::ObjProp prop) : EventFor{prop} {}
};

}  // namespace apptraverse::ui

#endif  // APPTRAVERSE_UI_UI_PUSH_BUTTON_H_
