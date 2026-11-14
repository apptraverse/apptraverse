#include "minimal_app_window_events.h"

#include "minimal_app_window_lifecycle.h"
#include "minimal_application.h"

namespace apptraverse::example::minimal {

APPTRAVERSE_REGISTER(MinimalOpenAuxiliaryWindowEvent);
APPTRAVERSE_REGISTER(MinimalCloseAuxiliaryWindowEvent);
APPTRAVERSE_REGISTER(MinimalViewportEvent);
APPTRAVERSE_REGISTER(MinimalDesktopEnvironmentEvent);

void EnsureMinimalAppWindowEventsRegistration() {
  (void)&g_apptraverse_registrar_MinimalOpenAuxiliaryWindowEvent;
  (void)&g_apptraverse_registrar_MinimalCloseAuxiliaryWindowEvent;
  (void)&g_apptraverse_registrar_MinimalViewportEvent;
  (void)&g_apptraverse_registrar_MinimalDesktopEnvironmentEvent;
}

void MinimalApplication::Apply(MinimalOpenAuxiliaryWindowEvent const&) {
  OpenAuxiliaryWindowImpl();
}

void MinimalApplication::Apply(MinimalCloseAuxiliaryWindowEvent const&) {
  CloseAuxiliaryWindowImpl();
}

}  // namespace apptraverse::example::minimal
