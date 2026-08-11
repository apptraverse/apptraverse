#ifndef APPTRAVERSE_OBJECT_STATE_H_
#define APPTRAVERSE_OBJECT_STATE_H_

#include <cstdint>
#include <vector>

#include "aether-miscpp/reflect/reflect.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/domain.h"
#include "aether/obj/obj_id.h"

#include "apptraverse/event.h"
#include "apptraverse/node.h"

namespace apptraverse {

struct StoredObjectVersion {
  ae::ObjId obj_id;
  std::uint32_t class_id{0};
  std::uint8_t version{0};
  ae::ObjectData data;

  AE_REFLECT_MEMBERS(obj_id, class_id, version, data)
};

// In-memory transfer value for one transferable root (Node or Event).
// Not a packet: no sender, peer, or transport metadata.
struct ObjectState {
  ae::ObjId root_id;
  std::vector<StoredObjectVersion> objects;

  AE_REFLECT_MEMBERS(root_id, objects)
};

using EventState = ObjectState;

// Ideal-memory endpoint: independent Domain + RamDomainStorage + shared root.
struct MemoryReplica {
  ae::Domain& domain;
  ae::RamDomainStorage& storage;
  ae::ObjId shared_root_id;
};

ObjectState CaptureNodeState(Node::ptr node,
                             ae::RamDomainStorage const& storage);

ObjectState CaptureEventState(Event::ptr event,
                              ae::RamDomainStorage const& storage);

void ImportObjectState(ObjectState const& state,
                       ae::RamDomainStorage& target_storage);

// Ideal-memory Event transfer: capture → import → AcceptRemoteEvent → save.
// Returns whether the Event was newly accepted (false on duplicate).
bool TransferRemoteEvent(Event::ptr source_event,
                         std::uint64_t original_timestamp_us,
                         ae::RamDomainStorage const& source_storage,
                         Node::ptr target_node,
                         ae::RamDomainStorage& target_storage);

}  // namespace apptraverse

#endif  // APPTRAVERSE_OBJECT_STATE_H_
