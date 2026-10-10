#include "apptraverse/ui/ui_edit_box.h"

namespace apptraverse::ui {

void UiEditBox::SubmitTextFromUi(std::string text,
                                 std::size_t caret_utf8_offset) {
  auto event =
      SetUiEditBoxTextEvent::ptr::Create(ae::CreateWith{*domain});
  event->text = std::move(text);
  Commit(std::move(event));
  SubmitCaretFromUi(caret_utf8_offset);
}

void UiEditBox::SubmitCaretFromUi(std::size_t caret_utf8_offset) {
  auto event =
      SetUiEditBoxCaretEvent::ptr::Create(ae::CreateWith{*domain});
  event->caret_utf8_offset = caret_utf8_offset;
  Commit(std::move(event));
}

void UiEditBox::Apply(SetUiEditBoxTextEvent const& event) {
  MaterializeText(event.text);
}

void UiEditBox::Apply(SetUiEditBoxCaretEvent const& event) {
  MaterializeCaret(event.caret_utf8_offset);
}

void UiEditBox::MaterializeText(std::string const& text) {
  if (text_ == text) {
    return;
  }
  text_ = text;
  NoteMaterializedChange();
}

void UiEditBox::MaterializeCaret(std::size_t caret_utf8_offset) {
  if (caret_utf8_offset_ == caret_utf8_offset) {
    return;
  }
  caret_utf8_offset_ = caret_utf8_offset;
  NoteMaterializedChange();
}

}  // namespace apptraverse::ui
