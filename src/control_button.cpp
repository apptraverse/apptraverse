#include "apptraverse/control_button.h"

#include "apptraverse/object_macros.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(ControlButton);
APPTRAVERSE_REGISTER(ControlButtonClickedEvent);

}  // namespace

void EnsureControlButtonRegistration() {
  static bool once = false;
  if (once) {
    return;
  }
  once = true;
  (void)&g_apptraverse_registrar_ControlButton;
  (void)&g_apptraverse_registrar_ControlButtonClickedEvent;
}

void ControlButton::RequestClick() {
  auto event =
      ControlButtonClickedEvent::ptr::Create(ae::CreateWith{*domain});
  Commit(event);
}

void ControlButton::Apply(ControlButtonClickedEvent const& event) {
  (void)event;
}

}  // namespace apptraverse
