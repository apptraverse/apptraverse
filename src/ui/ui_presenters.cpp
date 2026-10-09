#include "apptraverse/ui/ui_presenters.h"

#include "apptraverse/model_object_proxy.h"

namespace apptraverse::ui {
namespace {

constexpr std::uint32_t kUiPushButtonCommand = 1;

}  // namespace

void UiDesktopWindowPresenter::SubmitFrameFromNative(std::int32_t x,
                                                     std::int32_t y,
                                                     std::int32_t width,
                                                     std::int32_t height) {
  if (!model_proxy || !window) {
    return;
  }
  model_proxy->Invoke(window->obj_id, &UiDesktopWindow::SubmitFrameFromNative, x,
                      y, width, height);
}

bool UiEditBoxPresenter::ReadyForPresentation() const {
  return window_presenter && window_presenter->presentation_loaded;
}

void UiEditBoxPresenter::SubmitTextFromUi(std::string text,
                                          std::size_t caret_utf8_offset) {
  if (!model_proxy || !edit_box) {
    return;
  }
  model_proxy->Invoke(edit_box->obj_id, &UiEditBox::SubmitTextFromUi,
                      std::move(text), caret_utf8_offset);
}

void UiEditBoxPresenter::SubmitCaretFromUi(std::size_t caret_utf8_offset) {
  if (!model_proxy || !edit_box) {
    return;
  }
  model_proxy->Invoke(edit_box->obj_id, &UiEditBox::SubmitCaretFromUi,
                      caret_utf8_offset);
}

bool UiPushButtonPresenter::ReadyForPresentation() const {
  return window_presenter && window_presenter->presentation_loaded;
}

bool UiPushButtonPresenter::OnCommand(std::uint32_t command_id,
                                      std::uint16_t notification_code) {
  (void)notification_code;
  if (command_id != kUiPushButtonCommand || !model_proxy || !button) {
    return false;
  }
  model_proxy->Invoke(button->obj_id, &UiPushButton::NotifyClicked);
  return true;
}

void UiPushButtonPresenter::SimulateClickForTest() {
  static_cast<void>(OnCommand(kUiPushButtonCommand, 0));
}

bool UiLabelPresenter::ReadyForPresentation() const {
  return window_presenter && window_presenter->presentation_loaded;
}

}  // namespace apptraverse::ui
