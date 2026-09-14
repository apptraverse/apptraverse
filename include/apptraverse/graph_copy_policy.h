#ifndef APPTRAVERSE_GRAPH_COPY_POLICY_H_
#define APPTRAVERSE_GRAPH_COPY_POLICY_H_

#include "aether-objects/obj/domain.h"

namespace apptraverse {

// Graph serialization scope lives on ae::DomainGraph::serialization_scope.
// Default DomainGraph construction is LocalPersistent. Network export builds:
//   ae::DomainGraph graph{&domain, ae::GraphSerializationScope::NetworkShared};
//
// ObjectLink / LocalPtr serializers read archive.buffer().domain_graph->
// serialization_scope. There is no process-global, static, or thread_local
// policy registry.

using GraphSerializationScope = ae::GraphSerializationScope;

}  // namespace apptraverse

#endif  // APPTRAVERSE_GRAPH_COPY_POLICY_H_
