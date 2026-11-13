#ifndef APPTRAVERSE_SHARED_GRAPH_H_
#define APPTRAVERSE_SHARED_GRAPH_H_

#include <cstdint>
#include <vector>

#include "aether-miscpp/reflect/reflect.h"
#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj.h"

namespace apptraverse {

// Native object layers, not a second encoding of object fields.
// SharedSyncMessage wraps this table with routing and delivery metadata.
struct SharedGraphSnapshot {
  ae::ObjId root_id;
  ae::RamDomainStorage::ObjClassData layers;

  AE_REFLECT_MEMBERS(root_id, layers)
};

bool CaptureSharedGraph(ae::Obj const& root, SharedGraphSnapshot& snapshot);

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

// Validate native class chains, deserialize all layers in a disposable Domain,
// and check that serialized references stay within the packet's closed graph.
// expected_root_base_class_id == 0 accepts any registered root class.
bool ValidateSharedGraphSnapshot(
    SharedGraphSnapshot const& snapshot,
    std::uint32_t expected_root_base_class_id = 0);

// Preserve native object identities. Known loaded/cold objects are reused and
// their layers are NEVER replaced with potentially stale packet state. All
// class/closure checks precede writes; only absent objects are installed.
// The single-computer slice assumes native IDs identify the same logical object
// across replicas. An unrelated ID with a different class is rejected; this is
// not a namespace/remapping scheme for independently colliding equal-class IDs.
bool ImportSharedGraphNewObjects(SharedGraphSnapshot const& snapshot,
                                 ae::Domain& domain,
                                 ae::IDomainStorage& storage,
                                 ae::Ptr<ae::Obj>& out_root);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_GRAPH_H_
