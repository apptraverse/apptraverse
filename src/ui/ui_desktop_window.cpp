#include "apptraverse/ui/ui_desktop_window.h"

namespace apptraverse::ui {

void UiDesktopWindow::Apply(SetUiDesktopWindowFrameEvent const& event) {
  bool changed = false;
  if (x_ != event.x) {
    x_ = event.x;
    changed = true;
  }
  if (y_ != event.y) {
    y_ = event.y;
    changed = true;
  }
  if (client_width() != event.client_width ||
      client_height() != event.client_height) {
    SetClientSizeInternal(event.client_width, event.client_height);
  } else if (changed) {
    NoteMaterializedChange();
  }
}

void UiDesktopWindow::SubmitFrameFromNative(std::int32_t x, std::int32_t y,
                                            std::int32_t client_width,
                                            std::int32_t client_height) {
  auto event = SetUiDesktopWindowFrameEvent::ptr::Create(
      ae::CreateWith{*domain});
  event->x = x;
  event->y = y;
  event->client_width = client_width;
  event->client_height = client_height;
  Commit(std::move(event));
}

}  // namespace apptraverse::ui
