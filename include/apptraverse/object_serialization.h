#ifndef APPTRAVERSE_OBJECT_SERIALIZATION_H_
#define APPTRAVERSE_OBJECT_SERIALIZATION_H_

#include <cstdint>
#include <vector>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/idomain_storage.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/node.h"
#include "apptraverse/publication_channel.h"

namespace apptraverse {

// Future optimization: serialize reflected concrete state without reflected base
// class for UI publication, so Node::base/journal do not enter the buffer.

// Single-object layers for initial UI shells that already exist.
void SerializeObjectToBuffer(ae::Obj const& object, ByteSink& out);
void DeserializeObjectFromBuffer(ae::Obj& object, ByteSource& in,
                                 ae::Domain& domain,
                                 ae::IDomainStorage& domain_storage);

// Full standard-save graph fragment for incremental publication (includes newly
// referenced objects). Appends a Node Generation table so UI can finalize every
// Node created or updated by the fragment (not only the outer changed root).
void SerializeObjectGraphToBuffer(ae::Obj const& root, ByteSink& out);
void DeserializeObjectGraphFromBuffer(ae::Obj& existing_root, ByteSource& in,
                                      ae::Domain& domain,
                                      ae::IDomainStorage& domain_storage);

void CollectReachableObjects(ae::Obj& root, std::vector<ae::Obj*>& out);
void CollectReachableNodes(ae::Obj& root, std::vector<Node*>& out);

void FinalizeUiNodeState(ae::Obj& object, std::uint64_t generation);

}  // namespace apptraverse

#endif  // APPTRAVERSE_OBJECT_SERIALIZATION_H_
