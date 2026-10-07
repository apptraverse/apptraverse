#ifndef APPTRAVERSE_CONTROL_TEXT_EDIT_H_
#define APPTRAVERSE_CONTROL_TEXT_EDIT_H_

#include <cstdint>
#include <string>

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {

// TextEdit stores valid UTF-8 in std::string. caret and selection_anchor are
// UTF-8 byte offsets at Unicode scalar value boundaries (never inside a
// multi-byte sequence). Grapheme-cluster editing is deferred.
class TextEdit;
class TextEditInsertEvent;
class TextEditDeleteBackwardEvent;
class TextEditDeleteForwardEvent;
class TextEditDeleteSelectionEvent;
class TextEditSetCaretEvent;
class TextEditSetSelectionEvent;
class TextEditClearEvent;
class TextEditReplaceRangeEvent;

class TextEdit : public NodeFor<TextEdit> {
  APPTRAVERSE_OBJECT(TextEdit, Node, 0)

 protected:
  TextEdit() = default;

 public:
  explicit TextEdit(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(text), AE_MMBR(caret), AE_MMBR(selection_anchor))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, text, caret, selection_anchor);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, text, caret, selection_anchor);
  }

  std::string text;
  std::uint32_t caret{0};
  std::uint32_t selection_anchor{0};

  bool HasSelection() const { return selection_anchor != caret; }

  void RequestInsert(std::string insert_text);
  void RequestDeleteBackward(std::uint32_t code_points);
  void RequestDeleteForward(std::uint32_t code_points);
  void RequestDeleteSelection();
  void RequestSetCaret(std::uint32_t byte_offset);
  void RequestSetSelection(std::uint32_t anchor_byte_offset, std::uint32_t active_byte_offset);
  void RequestClear();
  void RequestReplaceRange(std::uint32_t start_byte_offset, std::uint32_t end_byte_offset,
                           std::string replacement);

  void Apply(TextEditInsertEvent const& event);
  void Apply(TextEditDeleteBackwardEvent const& event);
  void Apply(TextEditDeleteForwardEvent const& event);
  void Apply(TextEditDeleteSelectionEvent const& event);
  void Apply(TextEditSetCaretEvent const& event);
  void Apply(TextEditSetSelectionEvent const& event);
  void Apply(TextEditClearEvent const& event);
  void Apply(TextEditReplaceRangeEvent const& event);
};

class TextEditInsertEvent : public EventFor<TextEdit, TextEditInsertEvent> {
  APPTRAVERSE_OBJECT(TextEditInsertEvent, Event, 0)

 protected:
  TextEditInsertEvent() = default;

 public:
  explicit TextEditInsertEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(insert_text))

  std::string insert_text;
};

class TextEditDeleteBackwardEvent
    : public EventFor<TextEdit, TextEditDeleteBackwardEvent> {
  APPTRAVERSE_OBJECT(TextEditDeleteBackwardEvent, Event, 0)

 protected:
  TextEditDeleteBackwardEvent() = default;

 public:
  explicit TextEditDeleteBackwardEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(code_points))

  std::uint32_t code_points{1};
};

class TextEditDeleteForwardEvent
    : public EventFor<TextEdit, TextEditDeleteForwardEvent> {
  APPTRAVERSE_OBJECT(TextEditDeleteForwardEvent, Event, 0)

 protected:
  TextEditDeleteForwardEvent() = default;

 public:
  explicit TextEditDeleteForwardEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(code_points))

  std::uint32_t code_points{1};
};

class TextEditDeleteSelectionEvent
    : public EventFor<TextEdit, TextEditDeleteSelectionEvent> {
  APPTRAVERSE_OBJECT(TextEditDeleteSelectionEvent, Event, 0)

 protected:
  TextEditDeleteSelectionEvent() = default;

 public:
  explicit TextEditDeleteSelectionEvent(ae::ObjProp prop) : EventFor{prop} {}
};

class TextEditSetCaretEvent : public EventFor<TextEdit, TextEditSetCaretEvent> {
  APPTRAVERSE_OBJECT(TextEditSetCaretEvent, Event, 0)

 protected:
  TextEditSetCaretEvent() = default;

 public:
  explicit TextEditSetCaretEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(index))

  std::uint32_t index{0};
};

class TextEditSetSelectionEvent
    : public EventFor<TextEdit, TextEditSetSelectionEvent> {
  APPTRAVERSE_OBJECT(TextEditSetSelectionEvent, Event, 0)

 protected:
  TextEditSetSelectionEvent() = default;

 public:
  explicit TextEditSetSelectionEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(anchor), AE_MMBR(active))

  std::uint32_t anchor{0};
  std::uint32_t active{0};
};

class TextEditClearEvent : public EventFor<TextEdit, TextEditClearEvent> {
  APPTRAVERSE_OBJECT(TextEditClearEvent, Event, 0)

 protected:
  TextEditClearEvent() = default;

 public:
  explicit TextEditClearEvent(ae::ObjProp prop) : EventFor{prop} {}
};

class TextEditReplaceRangeEvent
    : public EventFor<TextEdit, TextEditReplaceRangeEvent> {
  APPTRAVERSE_OBJECT(TextEditReplaceRangeEvent, Event, 0)

 protected:
  TextEditReplaceRangeEvent() = default;

 public:
  explicit TextEditReplaceRangeEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(start), AE_MMBR(end), AE_MMBR(replacement))

  std::uint32_t start{0};
  std::uint32_t end{0};
  std::string replacement;
};

void EnsureTextEditControlRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_CONTROL_TEXT_EDIT_H_
