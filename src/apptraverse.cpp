#include "apptraverse/object/object_macros.h"

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object/event.h"
#include "apptraverse/object/node.h"
#include "apptraverse/presenter.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Event);
APPTRAVERSE_REGISTER(Node);
APPTRAVERSE_REGISTER(Presenter);

}  // namespace

void ForceLifecycleRegistration();
void ForceLinkRegistration();
void ForceSharedNodeRegistration();

// Forces the static library object file (and its Registrars) to be linked.
void EnsureObjectRegistration() {
  EnableNoninteractiveCrt();
  ForceLifecycleRegistration();
  ForceLinkRegistration();
  ForceSharedNodeRegistration();
}

}  // namespace apptraverse
