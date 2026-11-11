#ifndef APPTRAVERSE_SHARED_GRAPH_H_
#define APPTRAVERSE_SHARED_GRAPH_H_

#include <cstdint>
#include <vector>

#include "aether-miscpp/reflect/reflect.h"
#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj.h"

namespace apptraverse {

// Native object layers, not a second encoding of object fields. This is the
// initial serialization primitive; packet routing, ACKs and compatibility
// messages will wrap it in the next synchronization slice.
struct SharedGraphSnapshot {
  ae::ObjId root_id;
  ae::RamDomainStorage::ObjClassData layers;

  AE_REFLECT_MEMBERS(root_id, layers)
};

// Captures a closed NetworkShared graph using DomainGraph and BinaryArchive.
// LocalPtr edges are omitted. The returned bytes are frozen and independent
// of the source Domain/storage after this call.
bool SerializeSharedGraph(ae::Obj const& root,
                          std::vector<std::uint8_t>& bytes);

// Parses into a temporary before publishing the result. Does not write to a
// live replica or implement object identity reconciliation. A fresh receiver
// can load these native layers with an ordinary NetworkShared DomainGraph.
bool DeserializeSharedGraph(std::vector<std::uint8_t> const& bytes,
                            SharedGraphSnapshot& snapshot);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_GRAPH_H_
