#ifndef APPTRAVERSE_UI_UI_LOCALE_SETTINGS_H_
#define APPTRAVERSE_UI_UI_LOCALE_SETTINGS_H_

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

namespace apptraverse::ui {

class SetUiLanguageIndexEvent;

class UiLocaleSettings : public NodeFor<UiLocaleSettings> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiLocaleSettings",
                           UiLocaleSettings, Node, 0)

 protected:
  UiLocaleSettings() = default;

 public:
  explicit UiLocaleSettings(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(language_index_))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, language_index_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, language_index_);
  }

  int language_index() const { return language_index_; }

  void Apply(SetUiLanguageIndexEvent const& event);

 private:
  int language_index_{0};
};

class SetUiLanguageIndexEvent
    : public EventFor<UiLocaleSettings, SetUiLanguageIndexEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::SetUiLanguageIndexEvent",
                           SetUiLanguageIndexEvent, Event, 0)

 protected:
  SetUiLanguageIndexEvent() = default;

 public:
  explicit SetUiLanguageIndexEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(language_index))

  int language_index{0};
};

}  // namespace apptraverse::ui

#endif  // APPTRAVERSE_UI_UI_LOCALE_SETTINGS_H_
