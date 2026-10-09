#include "apptraverse/ui/ui_window.h"

namespace apptraverse::ui {

void UiWindow::SetClientSizeInternal(std::int32_t width, std::int32_t height) {
  if (client_width_ == width && client_height_ == height) {
    return;
  }
  client_width_ = width;
  client_height_ = height;
  NoteMaterializedChange();
}

void UiWindow::Apply(SetUiWindowClientSizeEvent const& event) {
  SetClientSizeInternal(event.width, event.height);
}

}  // namespace apptraverse::ui
