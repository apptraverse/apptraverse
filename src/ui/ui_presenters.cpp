#include "apptraverse/ui/ui_presenters.h"

#include "apptraverse/model_object_proxy.h"

namespace apptraverse::ui {

void UiDesktopWindowPresenter::SubmitFrameFromNative(
    std::int32_t x, std::int32_t y, std::int32_t client_width,
    std::int32_t client_height) {
  if (model_proxy == nullptr) {
    return;
  }
  UiDesktopWindow::ptr const desktop = desktop_window();
  model_proxy->Invoke(desktop->obj_id, &UiDesktopWindow::SubmitFrameFromNative,
                      x, y, client_width, client_height);
}

bool UiListContainerPresenter::ReadyForPresentation() const {
  if (!window_presenter || !window_presenter.is_loaded()) {
    return false;
  }
  Presenter const* const window =
      static_cast<Presenter const*>(window_presenter.operator->());
  return window != nullptr && window->presentation_loaded;
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
  model_proxy->Invoke(container->obj_id,
                      &UiListContainer::SubmitListRowSelectionChangedFromUi,
                      row_index);
}

void UiListContainerPresenter::OnListRowActivated(std::size_t row_index) {
  static_cast<void>(row_index);
}

void UiListContainerPresenter::OnListRowSelectionChanged(
    std::size_t row_index) {
  static_cast<void>(row_index);
}

}  // namespace apptraverse::ui
