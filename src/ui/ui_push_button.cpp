#include "apptraverse/ui/ui_push_button.h"

namespace apptraverse::ui {

void UiPushButton::NotifyClicked() {
  if (!enabled()) {
    return;
  }
  HandlePushButtonClicked();
}

void UiPushButton::HandlePushButtonClicked() {}

void UiPushButton::Apply(UiPushButtonClickedEvent const&) {}

void UiPushButton::Apply(SetUiPushButtonEnabledEvent const& event) {
  MaterializeEnabled(event.enabled);
}

void UiPushButton::MaterializeEnabled(bool enabled) {
  if (enabled_ == enabled) {
    return;
  }
  enabled_ = enabled;
  NoteMaterializedChange();
}

}  // namespace apptraverse::ui
