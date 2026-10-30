#include "minimal_app_window_events.h"

#include "minimal_app_window_lifecycle.h"
#include "minimal_application.h"

namespace apptraverse::example::minimal {

APPTRAVERSE_REGISTER(MinimalOpenAuxiliaryWindowEvent);
APPTRAVERSE_REGISTER(MinimalCloseAuxiliaryWindowEvent);

void EnsureMinimalAppWindowEventsRegistration() {
  (void)&g_apptraverse_registrar_MinimalOpenAuxiliaryWindowEvent;
  (void)&g_apptraverse_registrar_MinimalCloseAuxiliaryWindowEvent;
}

void MinimalApplication::Apply(MinimalOpenAuxiliaryWindowEvent const&) {
  OpenAuxiliaryWindowImpl();
}

void MinimalApplication::Apply(MinimalCloseAuxiliaryWindowEvent const&) {
  CloseAuxiliaryWindowImpl();
}

}  // namespace apptraverse::example::minimal
