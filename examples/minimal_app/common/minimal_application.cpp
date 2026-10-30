#include "minimal_application.h"

#include "minimal_app_controls.h"

#include "apptraverse/object_macros.h"

namespace apptraverse::example::minimal {

ae::ObjId MinimalApplicationRootId() { return ae::ObjId{0x07A10001u}; }

ae::ObjId MinimalMainWindowId() { return ae::ObjId{0x07A10002u}; }

ae::ObjId MinimalPresenterId() { return ae::ObjId{0x07A10003u}; }

APPTRAVERSE_REGISTER(MinimalApplication);

void EnsureMinimalApplicationRegistration() {
  EnsureMinimalAppControlsRegistration();
  (void)&g_apptraverse_registrar_MinimalApplication;
}

}  // namespace apptraverse::example::minimal
