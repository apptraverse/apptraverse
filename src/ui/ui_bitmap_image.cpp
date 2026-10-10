#include "apptraverse/ui/ui_bitmap_image.h"

namespace apptraverse::ui {

bool UiBitmapImagePresenter::ReadyForPresentation() const {
  return window_presenter && window_presenter->presentation_loaded;
}

void UiBitmapImagePresenter::OnModelChanged() {}

}  // namespace apptraverse::ui
