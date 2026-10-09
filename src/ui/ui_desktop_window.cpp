#include "apptraverse/ui/ui_desktop_window.h"

namespace apptraverse::ui {

void UiDesktopWindow::Apply(SetUiDesktopWindowFrameEvent const& event) {
  x_ = event.x;
  y_ = event.y;
  if (window) {
    auto size_event = SetUiWindowClientSizeEvent::ptr::Create(
        ae::CreateWith{*window->domain});
    size_event->width = event.width;
    size_event->height = event.height;
    window->Commit(std::move(size_event));
  }
}

void UiDesktopWindow::SubmitFrameFromNative(std::int32_t x, std::int32_t y,
                                            std::int32_t width,
                                            std::int32_t height) {
  auto event = SetUiDesktopWindowFrameEvent::ptr::Create(
      ae::CreateWith{*domain});
  event->x = x;
  event->y = y;
  event->width = width;
  event->height = height;
  Commit(std::move(event));
}

}  // namespace apptraverse::ui
