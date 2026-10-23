#include "apptraverse/ui/ui_presenters.h"

#include "apptraverse/model_object_proxy.h"

namespace apptraverse::ui {

void UiDesktopWindowPresenter::SubmitFrameFromNative(
    std::int32_t x, std::int32_t y, std::int32_t client_width,
    std::int32_t client_height) {
  UiDesktopWindow::ptr const desktop = desktop_window();
  model_proxy->Invoke(desktop->obj_id, &UiDesktopWindow::SubmitFrameFromNative,
                      x, y, client_width, client_height);
}

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

bool UiListContainerPresenter::ReadyForPresentation() const {
  return window_presenter->presentation_loaded;
}

void UiListContainerPresenter::OnModelChanged() {}

void UiListContainerPresenter::SubmitListRowActivatedFromUi(
    std::size_t row_index) {
  model_proxy->Invoke(container->obj_id,
                      &UiListContainer::SubmitListRowActivatedFromUi,
                      row_index);
}

void UiListContainerPresenter::SubmitListRowSelectionChangedFromUi(
    std::size_t row_index) {
  OnListRowSelectionChanged(row_index);
}
// Default selection stays local until a platform presenter overrides.

void UiListContainerPresenter::OnListRowActivated(std::size_t row_index) {
  static_cast<void>(row_index);
}

void UiListContainerPresenter::OnListRowSelectionChanged(
    std::size_t row_index) {
  static_cast<void>(row_index);
}

}  // namespace apptraverse::ui
