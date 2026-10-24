#include "apptraverse/ui/ui_presenters.h"

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/ui/ui_edit_box.h"

namespace apptraverse::ui {

bool UiEditBoxPresenter::ReadyForPresentation() const {
  return window_presenter->presentation_loaded;
}

void UiEditBoxPresenter::SubmitTextFromUi(std::string text,
                                          std::size_t caret_utf8_offset) {
  model_proxy->Invoke(edit_box->obj_id, &UiEditBox::SubmitTextFromUi,
                      std::move(text), caret_utf8_offset);
}

void UiEditBoxPresenter::SubmitCaretFromUi(std::size_t caret_utf8_offset) {
  model_proxy->Invoke(edit_box->obj_id, &UiEditBox::SubmitCaretFromUi,
                      caret_utf8_offset);
}

}  // namespace apptraverse::ui
