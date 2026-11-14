#include "apptraverse/ui/ui_desktop_window.h"

namespace apptraverse::ui {

void UiDesktopWindow::ApplyRestoreFrameGeometry(std::int32_t x, std::int32_t y,
                                                std::int32_t width,
                                                std::int32_t height) {
  bool changed = false;
  if (x_ != x) {
    x_ = x;
    changed = true;
  }
  if (y_ != y) {
    y_ = y;
    changed = true;
  }
  if (restore_client_width_ != width) {
    restore_client_width_ = width;
    changed = true;
  }
  if (restore_client_height_ != height) {
    restore_client_height_ = height;
    changed = true;
  }
  if (changed) {
    NoteMaterializedChange();
  }
}

void UiDesktopWindow::ApplyLiveClientSize(std::int32_t width,
                                          std::int32_t height) {
  if (client_width() == width && client_height() == height) {
    return;
  }
  SetClientSizeInternal(width, height);
}

void UiDesktopWindow::Apply(SetUiDesktopWindowFrameEvent const& event) {
  if (!HasExplicitPosition()) {
    placement_ = InitialPlacement::Explicit;
    NoteMaterializedChange();
  }
  ApplyRestoreFrameGeometry(event.x, event.y, event.client_width,
                            event.client_height);
  ApplyLiveClientSize(event.client_width, event.client_height);
}

void UiDesktopWindow::Apply(
    ReportUiDesktopWindowFrameFromNativeEvent const& event) {
  if (event.updates_restore_geometry) {
    if (placement_ == InitialPlacement::OperatingSystem) {
      placement_ = InitialPlacement::Explicit;
      NoteMaterializedChange();
    }
    ApplyRestoreFrameGeometry(event.x, event.y, event.client_width,
                              event.client_height);
  }
  ApplyLiveClientSize(event.client_width, event.client_height);
}

void UiDesktopWindow::Apply(RequestUiDesktopWindowFrameEvent const& event) {
  placement_ = InitialPlacement::Explicit;
  NoteMaterializedChange();
  ApplyRestoreFrameGeometry(event.x, event.y, event.client_width,
                            event.client_height);
  ApplyLiveClientSize(event.client_width, event.client_height);
}

void UiDesktopWindow::SubmitFrameFromNative(std::int32_t x, std::int32_t y,
                                            std::int32_t client_width,
                                            std::int32_t client_height,
                                            bool updates_restore_geometry) {
  bool const live_unchanged =
      this->client_width() == client_width &&
      this->client_height() == client_height;
  bool const restore_unchanged =
      !updates_restore_geometry ||
      (x_ == x && y_ == y && restore_client_width_ == client_width &&
       restore_client_height_ == client_height);
  if (live_unchanged &&
      (!updates_restore_geometry || (restore_unchanged && HasExplicitPosition()))) {
    return;
  }
  auto event = ReportUiDesktopWindowFrameFromNativeEvent::ptr::Create(
      ae::CreateWith{*domain});
  event->x = x;
  event->y = y;
  event->client_width = client_width;
  event->client_height = client_height;
  event->updates_restore_geometry = updates_restore_geometry;
  Commit(std::move(event));
}

void UiDesktopWindow::RequestFrame(std::int32_t x, std::int32_t y,
                                   std::int32_t client_width,
                                   std::int32_t client_height) {
  if (HasExplicitPosition() && x_ == x && y_ == y && restore_client_width_ == client_width &&
      restore_client_height_ == client_height &&
      this->client_width() == client_width &&
      this->client_height() == client_height) {
    return;
  }
  auto event = RequestUiDesktopWindowFrameEvent::ptr::Create(
      ae::CreateWith{*domain});
  event->x = x;
  event->y = y;
  event->client_width = client_width;
  event->client_height = client_height;
  Commit(std::move(event));
  frame_request_pending_publication_ = true;
}

void UiDesktopWindow::Apply(ReportUiDesktopScreenEvent const& event) {
  // The backend reports measurements; placement policy belongs to the model.
  if (placement_ == InitialPlacement::Centered) {
    ApplyRestoreFrameGeometry(event.x + (event.width - restore_client_width_) / 2,
                              event.y + (event.height - restore_client_height_) / 2,
                              restore_client_width_, restore_client_height_);
    placement_ = InitialPlacement::Explicit;
    NoteMaterializedChange();
    frame_request_pending_publication_ = true;
  }
}

void UiDesktopWindow::SubmitScreenFromNative(std::int32_t x, std::int32_t y,
                                             std::int32_t width, std::int32_t height) {
  auto event = ReportUiDesktopScreenEvent::ptr::Create(ae::CreateWith{*domain});
  event->x = x;
  event->y = y;
  event->width = width;
  event->height = height;
  Commit(std::move(event));
}

}  // namespace apptraverse::ui
