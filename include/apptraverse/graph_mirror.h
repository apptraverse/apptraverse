#ifndef APPTRAVERSE_GRAPH_MIRROR_H_
#define APPTRAVERSE_GRAPH_MIRROR_H_

#include "aether/obj/domain.h"
#include "aether/obj/idomain_storage.h"
#include "aether/obj/obj.h"

namespace apptraverse {

// One-time copy of the full model application graph into the UI domain.
// Same ObjIds and concrete classes; different Domain and C++ addresses.
// UI Node objects get model Generation with empty base/journal.
// Returns a strong reference to the UI root (Domain stores weak refs only).
ae::Ptr<ae::Obj> CopyModelGraphToUiDomain(ae::Obj& model_root,
                                          ae::Domain& ui_domain,
                                          ae::IDomainStorage& ui_storage);

}  // namespace apptraverse

#endif  // APPTRAVERSE_GRAPH_MIRROR_H_
