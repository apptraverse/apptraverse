#include "apptraverse/control_text_edit.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "apptraverse/object_macros.h"
#include "apptraverse/utf8_text.h"

namespace apptraverse {
namespace {

[[noreturn]] void TextEditFatalInvalidUtf8(char const* context) {
  std::fprintf(stderr, "TextEdit invariant: invalid UTF-8 (%s)\n", context);
  std::abort();
}

void RequireValidUtf8(std::string const& value, char const* context) {
  if (!IsValidUtf8(value)) {
    TextEditFatalInvalidUtf8(context);
  }
}

std::uint32_t ClampByteOffset(std::string const& text, std::uint32_t offset) {
  return static_cast<std::uint32_t>(
      std::min<std::size_t>(offset, text.size()));
}

std::uint32_t SnapBoundary(std::string const& text, std::uint32_t offset) {
  return static_cast<std::uint32_t>(SnapForwardToUtf8Boundary(text, offset));
}

std::pair<std::uint32_t, std::uint32_t> NormalizeByteRange(std::string const& text,
                                                           std::uint32_t a,
                                                           std::uint32_t b) {
  a = SnapBoundary(text, a);
  b = SnapBoundary(text, b);
  if (a <= b) {
    return {a, b};
  }
  return {b, a};
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

void TextEdit::RequestInsert(std::string insert_text) {
  RequireValidUtf8(insert_text, "RequestInsert");
  auto event = TextEditInsertEvent::ptr::Create(ae::CreateWith{*domain});
  event->insert_text = std::move(insert_text);
  Commit(event);
}

void TextEdit::RequestDeleteBackward(std::uint32_t code_points) {
  auto event = TextEditDeleteBackwardEvent::ptr::Create(ae::CreateWith{*domain});
  event->code_points = code_points == 0 ? 1 : code_points;
  Commit(event);
}

void TextEdit::RequestDeleteForward(std::uint32_t code_points) {
  auto event = TextEditDeleteForwardEvent::ptr::Create(ae::CreateWith{*domain});
  event->code_points = code_points == 0 ? 1 : code_points;
  Commit(event);
}

void TextEdit::RequestDeleteSelection() {
  auto event =
      TextEditDeleteSelectionEvent::ptr::Create(ae::CreateWith{*domain});
  Commit(event);
}

void TextEdit::RequestSetCaret(std::uint32_t byte_offset) {
  auto event = TextEditSetCaretEvent::ptr::Create(ae::CreateWith{*domain});
  event->index = SnapBoundary(text, byte_offset);
  Commit(event);
}

void TextEdit::RequestSetSelection(std::uint32_t anchor_byte_offset,
                                   std::uint32_t active_byte_offset) {
  auto event = TextEditSetSelectionEvent::ptr::Create(ae::CreateWith{*domain});
  event->anchor = SnapBoundary(text, anchor_byte_offset);
  event->active = SnapBoundary(text, active_byte_offset);
  Commit(event);
}

void TextEdit::RequestClear() {
  auto event = TextEditClearEvent::ptr::Create(ae::CreateWith{*domain});
  Commit(event);
}

void TextEdit::RequestReplaceRange(std::uint32_t start_byte_offset,
                                   std::uint32_t end_byte_offset,
                                   std::string replacement) {
  RequireValidUtf8(replacement, "RequestReplaceRange");
  auto event =
      TextEditReplaceRangeEvent::ptr::Create(ae::CreateWith{*domain});
  event->start = SnapBoundary(text, start_byte_offset);
  event->end = SnapBoundary(text, end_byte_offset);
  event->replacement = std::move(replacement);
  Commit(event);
}

void TextEdit::Apply(TextEditInsertEvent const& event) {
  RequireValidUtf8(event.insert_text, "TextEditInsertEvent");
  if (HasSelection()) {
    auto const [start, end] = NormalizeByteRange(text, selection_anchor, caret);
    text.erase(start, end - start);
    caret = start;
    selection_anchor = start;
  }
  std::uint32_t const at = SnapBoundary(text, caret);
  text.insert(at, event.insert_text);
  caret = at + static_cast<std::uint32_t>(event.insert_text.size());
  selection_anchor = caret;
  NoteMaterializedChange();
}

void TextEdit::Apply(TextEditDeleteBackwardEvent const& event) {
  if (HasSelection()) {
    auto const [start, end] = NormalizeByteRange(text, selection_anchor, caret);
    text.erase(start, end - start);
    caret = start;
    selection_anchor = start;
    NoteMaterializedChange();
    return;
  }
  std::uint32_t const end = SnapBoundary(text, caret);
  std::uint32_t at = end;
  std::uint32_t count = event.code_points == 0 ? 1 : event.code_points;
  while (count > 0 && at > 0) {
    at = static_cast<std::uint32_t>(PreviousUtf8CodePointBoundary(text, at));
    --count;
  }
  text.erase(at, end - at);
  caret = at;
  selection_anchor = at;
  NoteMaterializedChange();
}

void TextEdit::Apply(TextEditDeleteForwardEvent const& event) {
  if (HasSelection()) {
    auto const [start, end] = NormalizeByteRange(text, selection_anchor, caret);
    text.erase(start, end - start);
    caret = start;
    selection_anchor = start;
    NoteMaterializedChange();
    return;
  }
  std::uint32_t at = SnapBoundary(text, caret);
  std::uint32_t end = at;
  std::uint32_t count = event.code_points == 0 ? 1 : event.code_points;
  while (count > 0 && end < text.size()) {
    end = static_cast<std::uint32_t>(NextUtf8CodePointBoundary(text, end));
    --count;
  }
  text.erase(at, end - at);
  caret = at;
  selection_anchor = at;
  NoteMaterializedChange();
}

void TextEdit::Apply(TextEditDeleteSelectionEvent const& event) {
  (void)event;
  if (!HasSelection()) {
    NoteMaterializedChange();
    return;
  }
  auto const [start, end] = NormalizeByteRange(text, selection_anchor, caret);
  text.erase(start, end - start);
  caret = start;
  selection_anchor = start;
  NoteMaterializedChange();
}

void TextEdit::Apply(TextEditSetCaretEvent const& event) {
  caret = SnapBoundary(text, event.index);
  selection_anchor = caret;
  NoteMaterializedChange();
}

void TextEdit::Apply(TextEditSetSelectionEvent const& event) {
  selection_anchor = SnapBoundary(text, event.anchor);
  caret = SnapBoundary(text, event.active);
  NoteMaterializedChange();
}

void TextEdit::Apply(TextEditClearEvent const& event) {
  (void)event;
  text.clear();
  caret = 0;
  selection_anchor = 0;
  NoteMaterializedChange();
}

void TextEdit::Apply(TextEditReplaceRangeEvent const& event) {
  RequireValidUtf8(event.replacement, "TextEditReplaceRangeEvent");
  auto const [start, end] =
      NormalizeByteRange(text, event.start, event.end);
  text.replace(start, end - start, event.replacement);
  caret = start + static_cast<std::uint32_t>(event.replacement.size());
  selection_anchor = caret;
  NoteMaterializedChange();
}

}  // namespace apptraverse
