#include "apptraverse/ui/ui_push_button.h"

namespace apptraverse::ui {

void UiPushButton::NotifyClicked() {
  auto event =
      UiPushButtonClickedEvent::ptr::Create(ae::CreateWith{*domain});
  Commit(std::move(event));
}

void UiPushButton::Apply(UiPushButtonClickedEvent const&) {}

}  // namespace apptraverse::ui
