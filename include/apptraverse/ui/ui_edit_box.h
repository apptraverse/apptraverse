#ifndef APPTRAVERSE_UI_UI_EDIT_BOX_H_
#define APPTRAVERSE_UI_UI_EDIT_BOX_H_

#include <cstddef>
#include <string>

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

namespace apptraverse::ui {

class SetUiEditBoxTextEvent;
class SetUiEditBoxCaretEvent;

class UiEditBox : public NodeFor<UiEditBox> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiEditBox", UiEditBox, Node, 0)

 protected:
  UiEditBox() = default;

 public:
  explicit UiEditBox(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(text_), AE_MMBR(caret_utf8_offset_),
                    AE_MMBR(read_only_))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, text_, caret_utf8_offset_, read_only_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, text_, caret_utf8_offset_, read_only_);
  }

  std::string const& text() const { return text_; }
  std::size_t caret_utf8_offset() const { return caret_utf8_offset_; }
  bool read_only() const { return read_only_; }

  void SubmitTextFromUi(std::string text, std::size_t caret_utf8_offset);
  void SubmitCaretFromUi(std::size_t caret_utf8_offset);
  void BootstrapSetReadOnly(bool read_only) { read_only_ = read_only; }

  void Apply(SetUiEditBoxTextEvent const& event);
  void Apply(SetUiEditBoxCaretEvent const& event);

 private:
  std::string text_;
  std::size_t caret_utf8_offset_{0};
  bool read_only_{false};
};

class SetUiEditBoxTextEvent : public EventFor<UiEditBox, SetUiEditBoxTextEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::SetUiEditBoxTextEvent",
                           SetUiEditBoxTextEvent, Event, 0)

 protected:
  SetUiEditBoxTextEvent() = default;

 public:
  explicit SetUiEditBoxTextEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(text))

  std::string text;
};

class SetUiEditBoxCaretEvent
    : public EventFor<UiEditBox, SetUiEditBoxCaretEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::SetUiEditBoxCaretEvent",
                           SetUiEditBoxCaretEvent, Event, 0)

 protected:
  SetUiEditBoxCaretEvent() = default;

 public:
  explicit SetUiEditBoxCaretEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(caret_utf8_offset))

  std::size_t caret_utf8_offset{0};
};

}  // namespace apptraverse::ui

#endif  // APPTRAVERSE_UI_UI_EDIT_BOX_H_
