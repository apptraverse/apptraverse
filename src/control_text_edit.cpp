#include "apptraverse/control_text_edit.h"

#include <algorithm>
#include <cassert>

#include "apptraverse/object_macros.h"

namespace apptraverse {
namespace {

std::uint32_t ClampIndex(std::u16string const& text, std::uint32_t index) {
  return std::min(index, static_cast<std::uint32_t>(text.size()));
}

std::pair<std::uint32_t, std::uint32_t> NormalizeRange(std::u16string const& text,
                                                       std::uint32_t a,
                                                       std::uint32_t b) {
  a = ClampIndex(text, a);
  b = ClampIndex(text, b);
  if (a <= b) {
    return {a, b};
  }
  return {b, a};
}

void EnsureValidUtf16Boundary(std::u16string const& text, std::uint32_t index) {
  if (index == 0 || index >= text.size()) {
    return;
  }
  if (index > 0 && text[index - 1] >= 0xD800 && text[index - 1] <= 0xDBFF) {
    assert(text[index] >= 0xDC00 && text[index] <= 0xDFFF);
  }
}

APPTRAVERSE_REGISTER(TextEdit);
APPTRAVERSE_REGISTER(TextEditInsertEvent);
APPTRAVERSE_REGISTER(TextEditDeleteBackwardEvent);
APPTRAVERSE_REGISTER(TextEditDeleteForwardEvent);
APPTRAVERSE_REGISTER(TextEditDeleteSelectionEvent);
APPTRAVERSE_REGISTER(TextEditSetCaretEvent);
APPTRAVERSE_REGISTER(TextEditSetSelectionEvent);
APPTRAVERSE_REGISTER(TextEditClearEvent);
APPTRAVERSE_REGISTER(TextEditReplaceRangeEvent);

}  // namespace

void EnsureTextEditControlRegistration() {
  static bool once = false;
  if (once) {
    return;
  }
  once = true;
  (void)&g_apptraverse_registrar_TextEdit;
  (void)&g_apptraverse_registrar_TextEditInsertEvent;
  (void)&g_apptraverse_registrar_TextEditDeleteBackwardEvent;
  (void)&g_apptraverse_registrar_TextEditDeleteForwardEvent;
  (void)&g_apptraverse_registrar_TextEditDeleteSelectionEvent;
  (void)&g_apptraverse_registrar_TextEditSetCaretEvent;
  (void)&g_apptraverse_registrar_TextEditSetSelectionEvent;
  (void)&g_apptraverse_registrar_TextEditClearEvent;
  (void)&g_apptraverse_registrar_TextEditReplaceRangeEvent;
}

void TextEdit::RequestInsert(std::u16string insert_text) {
  auto event = TextEditInsertEvent::ptr::Create(ae::CreateWith{*domain});
  event->insert_text = std::move(insert_text);
  Commit(event);
}

void TextEdit::RequestDeleteBackward(std::uint32_t code_units) {
  auto event = TextEditDeleteBackwardEvent::ptr::Create(ae::CreateWith{*domain});
  event->code_units = code_units == 0 ? 1 : code_units;
  Commit(event);
}

void TextEdit::RequestDeleteForward(std::uint32_t code_units) {
  auto event = TextEditDeleteForwardEvent::ptr::Create(ae::CreateWith{*domain});
  event->code_units = code_units == 0 ? 1 : code_units;
  Commit(event);
}

void TextEdit::RequestDeleteSelection() {
  auto event =
      TextEditDeleteSelectionEvent::ptr::Create(ae::CreateWith{*domain});
  Commit(event);
}

void TextEdit::RequestSetCaret(std::uint32_t index) {
  auto event = TextEditSetCaretEvent::ptr::Create(ae::CreateWith{*domain});
  event->index = index;
  Commit(event);
}

void TextEdit::RequestSetSelection(std::uint32_t anchor, std::uint32_t active) {
  auto event = TextEditSetSelectionEvent::ptr::Create(ae::CreateWith{*domain});
  event->anchor = anchor;
  event->active = active;
  Commit(event);
}

void TextEdit::RequestClear() {
  auto event = TextEditClearEvent::ptr::Create(ae::CreateWith{*domain});
  Commit(event);
}

void TextEdit::RequestReplaceRange(std::uint32_t start, std::uint32_t end,
                                   std::u16string replacement) {
  auto event =
      TextEditReplaceRangeEvent::ptr::Create(ae::CreateWith{*domain});
  event->start = start;
  event->end = end;
  event->replacement = std::move(replacement);
  Commit(event);
}

void TextEdit::Apply(TextEditInsertEvent const& event) {
  std::uint32_t const at = ClampIndex(text, caret);
  EnsureValidUtf16Boundary(text, at);
  text.insert(at, event.insert_text);
  caret = at + static_cast<std::uint32_t>(event.insert_text.size());
  selection_anchor = caret;
}

void TextEdit::Apply(TextEditDeleteBackwardEvent const& event) {
  std::uint32_t at = ClampIndex(text, caret);
  std::uint32_t count = event.code_units == 0 ? 1 : event.code_units;
  while (count > 0 && at > 0) {
    EnsureValidUtf16Boundary(text, at);
    if (at > 0 && text[at - 1] >= 0xDC00 && text[at - 1] <= 0xDFFF &&
        at > 1 && text[at - 2] >= 0xD800 && text[at - 2] <= 0xDBFF) {
      at -= 2;
    } else {
      --at;
    }
    --count;
  }
  text.erase(at, caret - at);
  caret = at;
  selection_anchor = at;
}

void TextEdit::Apply(TextEditDeleteForwardEvent const& event) {
  std::uint32_t at = ClampIndex(text, caret);
  std::uint32_t end = at;
  std::uint32_t count = event.code_units == 0 ? 1 : event.code_units;
  while (count > 0 && end < text.size()) {
    EnsureValidUtf16Boundary(text, end);
    if (end + 1 < text.size() && text[end] >= 0xD800 && text[end] <= 0xDBFF &&
        text[end + 1] >= 0xDC00 && text[end + 1] <= 0xDFFF) {
      end += 2;
    } else {
      ++end;
    }
    --count;
  }
  text.erase(at, end - at);
  caret = at;
  selection_anchor = at;
}

void TextEdit::Apply(TextEditDeleteSelectionEvent const& event) {
  (void)event;
  if (!HasSelection()) {
    return;
  }
  auto const [start, end] = NormalizeRange(text, selection_anchor, caret);
  text.erase(start, end - start);
  caret = start;
  selection_anchor = start;
}

void TextEdit::Apply(TextEditSetCaretEvent const& event) {
  caret = ClampIndex(text, event.index);
  EnsureValidUtf16Boundary(text, caret);
  selection_anchor = caret;
}

void TextEdit::Apply(TextEditSetSelectionEvent const& event) {
  selection_anchor = ClampIndex(text, event.anchor);
  caret = ClampIndex(text, event.active);
  EnsureValidUtf16Boundary(text, selection_anchor);
  EnsureValidUtf16Boundary(text, caret);
}

void TextEdit::Apply(TextEditClearEvent const& event) {
  (void)event;
  text.clear();
  caret = 0;
  selection_anchor = 0;
}

void TextEdit::Apply(TextEditReplaceRangeEvent const& event) {
  auto const [start, end] = NormalizeRange(text, event.start, event.end);
  text.replace(start, end - start, event.replacement);
  caret = start + static_cast<std::uint32_t>(event.replacement.size());
  selection_anchor = caret;
}

}  // namespace apptraverse
