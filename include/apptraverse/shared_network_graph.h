#ifndef APPTRAVERSE_SHARED_NETWORK_GRAPH_H_
#define APPTRAVERSE_SHARED_NETWORK_GRAPH_H_

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/idomain_storage.h"

#include "apptraverse/shared_node.h"

namespace apptraverse {

// Copy the shared/network view of a SharedNode into an independent Domain /
// storage. Includes SharedNode business state, shares[], and reachable Link
// objects. Excludes LocalPtr targets (per-Link local sync metadata).
//
// Never mutates the live source graph: sanitization runs on a scratch copy.
// This is not a transport; it proves the serialization boundary for later
// initial-state sync.
void CopySharedNetworkGraph(SharedNode::ptr source,
                            ae::IDomainStorage& source_storage,
                            ae::Domain& target_domain,
                            ae::IDomainStorage& target_storage);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_NETWORK_GRAPH_H_
