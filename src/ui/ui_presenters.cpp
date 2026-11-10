#include "apptraverse/ui/ui_presenters.h"

#include "apptraverse/model_object_proxy.h"

namespace apptraverse::ui {

void UiDesktopWindowPresenter::ReportNativeFrameToModel(
    std::int32_t x, std::int32_t y, std::int32_t client_width,
    std::int32_t client_height, bool updates_restore_geometry) {
  SubmitFrameFromNative(x, y, client_width, client_height,
                        updates_restore_geometry);
}

void UiDesktopWindowPresenter::SubmitFrameFromNative(
    std::int32_t x, std::int32_t y, std::int32_t client_width,
    std::int32_t client_height, bool updates_restore_geometry) {
  UiDesktopWindow::ptr const desktop = desktop_window();
  model_proxy->Invoke(desktop->obj_id, &UiDesktopWindow::SubmitFrameFromNative,
                      x, y, client_width, client_height,
                      updates_restore_geometry);
}

void UiDesktopWindowPresenter::SubmitScreenFromNative(
    std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
  model_proxy->Invoke(desktop_window()->obj_id,
                      &UiDesktopWindow::SubmitScreenFromNative,
                      x, y, width, height);
}

bool UiListContainerPresenter::ReadyForPresentation() const {
  return window_presenter->presentation_loaded;
}

void UiListContainerPresenter::OnModelChanged() {}

std::optional<ae::ObjId> UiListContainerPresenter::RowIdAtUiIndex(
    std::size_t row_index) const {
  if (row_index >= container->rows.size()) { return std::nullopt; }
  return container->rows[row_index].id();
}

void UiListContainerPresenter::SubmitListRowActivatedFromUi(
    std::size_t row_index) {
  auto const row_id = RowIdAtUiIndex(row_index);
  if (!row_id) { return; }
  model_proxy->Invoke(container->obj_id,
                      &UiListContainer::SubmitListRowActivatedById, *row_id);
}

void UiListContainerPresenter::SubmitListRowSelectionChangedFromUi(
    std::size_t row_index) {
  auto const row_id = RowIdAtUiIndex(row_index);
  if (!row_id) { return; }
  model_proxy->Invoke(container->obj_id,
                      &UiListContainer::SubmitListRowSelectionChangedById, *row_id);
}

void UiListContainerPresenter::OnListRowActivated(std::size_t row_index) {
  static_cast<void>(row_index);
}

void UiListContainerPresenter::OnListRowSelectionChanged(
    std::size_t row_index) {
  static_cast<void>(row_index);
}

std::string UiListContainerPresenter::ResolveListRowPrimaryText(
    UiListRow::ptr const& row, int language_index) const {
  return ResolvePresentedListRowPrimaryText(row, language_index);
}

std::vector<std::string> UiListContainerPresenter::ResolveListRowPrimaryTexts(
    int language_index) const {
  container.Load();
  return ResolvePresentedListRowPrimaryTexts(*container, language_index);
}

}  // namespace apptraverse::ui
