#include "apptraverse/ui/ui_desktop_window.h"

namespace apptraverse::ui {

void UiDesktopWindow::ApplyFrameGeometry(std::int32_t x, std::int32_t y,
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
  if (client_width() != width || client_height() != height) {
    SetClientSizeInternal(width, height);
  } else if (changed) {
    NoteMaterializedChange();
  }
}

void UiDesktopWindow::Apply(SetUiDesktopWindowFrameEvent const& event) {
  ApplyFrameGeometry(event.x, event.y, event.client_width,
                     event.client_height);
}

void UiDesktopWindow::Apply(
    ReportUiDesktopWindowFrameFromNativeEvent const& event) {
  ApplyFrameGeometry(event.x, event.y, event.client_width,
                     event.client_height);
}

void UiDesktopWindow::Apply(RequestUiDesktopWindowFrameEvent const& event) {
  ApplyFrameGeometry(event.x, event.y, event.client_width,
                     event.client_height);
}

void UiDesktopWindow::SubmitFrameFromNative(std::int32_t x, std::int32_t y,
                                            std::int32_t client_width,
                                            std::int32_t client_height) {
  if (x_ == x && y_ == y && this->client_width() == client_width &&
      this->client_height() == client_height) {
    return;
  }
  auto event = ReportUiDesktopWindowFrameFromNativeEvent::ptr::Create(
      ae::CreateWith{*domain});
  event->x = x;
  event->y = y;
  event->client_width = client_width;
  event->client_height = client_height;
  Commit(std::move(event));
}

void UiDesktopWindow::RequestFrame(std::int32_t x, std::int32_t y,
                                   std::int32_t client_width,
                                   std::int32_t client_height) {
  if (x_ == x && y_ == y && this->client_width() == client_width &&
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

}  // namespace apptraverse::ui
