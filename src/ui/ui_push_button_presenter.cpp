#include "apptraverse/ui/ui_push_button_presenter.h"

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui {

bool UiPushButtonPresenter::ReadyForPresentation() const {
  return window_presenter->presentation_loaded;
}

void UiPushButtonPresenter::NotifyClickedFromUi() {
  if (!button->enabled()) {
    return;
  }
  button->QueueClickFromUi(*model_proxy);
}

void UiPushButtonPresenter::SimulateClickForTest() {
  NotifyClickedFromUi();
}

}  // namespace apptraverse::ui
