#include "apptraverse/ui/ui_window.h"

namespace apptraverse::ui {

void UiWindow::Apply(SetUiWindowClientSizeEvent const& event) {
  client_width_ = event.width;
  client_height_ = event.height;
}

}  // namespace apptraverse::ui
