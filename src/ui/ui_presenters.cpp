#include "apptraverse/ui/ui_presenters.h"

#include "apptraverse/model_object_proxy.h"

namespace apptraverse::ui {
namespace {

constexpr std::uint16_t kEnChange = 0x0300;
constexpr std::uint16_t kBnClicked = 0;

}  // namespace

void UiDesktopWindowPresenter::SubmitFrameFromNative(
    std::int32_t x, std::int32_t y, std::int32_t client_width,
    std::int32_t client_height) {
  auto desktop = desktop_window();
  if (!model_proxy || !desktop) {
    return;
  }
  model_proxy->Invoke(desktop->obj_id, &UiDesktopWindow::SubmitFrameFromNative,
                      x, y, client_width, client_height);
}

bool UiEditBoxPresenter::ReadyForPresentation() const {
  return window_presenter && window_presenter->presentation_loaded;
}

bool UiEditBoxPresenter::OnCommand(std::uint32_t command_id,
                                   std::uint16_t notification_code) {
  if (control_id == 0 || command_id != control_id ||
      notification_code != kEnChange) {
    return false;
  }
  return true;
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
  if (control_id == 0 || command_id != control_id ||
      notification_code != kBnClicked || !button || !button->enabled()) {
    return false;
  }
  if (!model_proxy) {
    return false;
  }
  model_proxy->Invoke(button->obj_id, &UiPushButton::NotifyClicked);
  return true;
}

void UiPushButtonPresenter::SimulateClickForTest() {
  static_cast<void>(OnCommand(control_id, kBnClicked));
}

}  // namespace apptraverse::ui
