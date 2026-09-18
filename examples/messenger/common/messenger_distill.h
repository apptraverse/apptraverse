#ifndef APPTRAVERSE_MESSENGER_DISTILL_H_
#define APPTRAVERSE_MESSENGER_DISTILL_H_

#include "aether-objects/obj/domain.h"

#include "messenger_model.h"

namespace apptraverse {

// Dev-only initial graph. Production load path never calls this.
Application::ptr BuildMessengerGraph(ae::Domain& domain);

}  // namespace apptraverse

#endif  // APPTRAVERSE_MESSENGER_DISTILL_H_
