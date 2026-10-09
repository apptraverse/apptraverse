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
  text_ = event.text;
}

void UiEditBox::Apply(SetUiEditBoxCaretEvent const& event) {
  caret_utf8_offset_ = event.caret_utf8_offset;
}

}  // namespace apptraverse::ui
