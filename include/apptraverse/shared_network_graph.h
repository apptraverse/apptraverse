#ifndef APPTRAVERSE_SHARED_NETWORK_GRAPH_H_
#define APPTRAVERSE_SHARED_NETWORK_GRAPH_H_

#include <cstdint>
#include <initializer_list>
#include <map>
#include <set>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/idomain_storage.h"
#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/obj_ptr.h"

#include "apptraverse/event.h"
#include "apptraverse/object_link.h"
#include "apptraverse/shared_event_id.h"
#include "apptraverse/shared_node.h"

namespace apptraverse {

// Serialize the live network-shared view of root into a scratch RAM storage.
// LocalPtr edges become empty/default; the source storage is untouched.
void BuildNetworkSharedScratch(ae::Obj const& root,
                               ae::RamDomainStorage& scratch);

// Serialize the network-shared view of a live object graph into target_storage.
// LocalPtr edges become empty/default and their referents are not exported.
// Does not write to the source Domain's storage (no source.Save side effect).
void CopyNetworkSharedObjectGraph(ae::Obj const& root,
                                  ae::IDomainStorage& target_storage);

// SharedNode convenience: includes business state, shares[], and reachable
// Link objects; excludes LocalPtr sync metadata. Scratch/target writes only.
void CopySharedNetworkGraph(SharedNode::ptr source,
                            ae::Domain& target_domain,
                            ae::IDomainStorage& target_storage);

// Serialize any RamDomainStorage state into transportable bytes using native
// Aether BinaryArchive serialization.
bool SerializeObjectGraph(ae::RamDomainStorage const& storage,
                          std::vector<std::uint8_t>& out);
std::vector<std::uint8_t> SerializeObjectGraph(
    ae::RamDomainStorage const& storage);

// Deserialize an untrusted object graph payload into scratch RamDomainStorage
// using native Aether BinaryArchive serialization. Returns false on error.
bool DeserializeObjectGraph(std::vector<std::uint8_t> const& payload,
                            ae::RamDomainStorage& storage);

// Same network-shared view, as transportable bytes produced through native
// Aether serialization.
std::vector<std::uint8_t> SerializeNetworkSharedObjectGraph(
    ae::Obj const& root);

struct FrozenNodeState {
  std::vector<std::uint8_t> payload;
  std::vector<SharedEventId> covered_event_ids;
};

// Freeze the network-shared snapshot and extract its covered SharedEventIds
// in a single operation from the SAME scratch state.
FrozenNodeState FreezeNetworkSharedNodeState(SharedNode const& root);

// Stored class hierarchy info for one object in scratch storage.
struct StoredClassChainInfo {
  ae::ObjId obj_id;
  std::vector<std::uint32_t> chain;  // sorted base -> derived
  std::uint32_t most_derived_class_id{0};
};

// Validate that every object stored in parsed has a coherent registered
// inheritance chain (no unrelated classes, object ID valid, at least one
// supported class, most-derived class is registered with create/load/save).
bool ValidateStoredClassChains(
    ae::RamDomainStorage const& parsed,
    std::vector<StoredClassChainInfo>* out_chains = nullptr);

// Write an already parsed graph into a replica's own storage.
void CommitObjectGraph(ae::RamDomainStorage const& parsed,
                       ae::IDomainStorage& target_storage);

// Parse, then commit. Malformed input leaves target_storage untouched: the
// whole payload is validated before the first write.
bool ImportObjectGraphPayload(std::vector<std::uint8_t> const& payload,
                              ae::IDomainStorage& target_storage);

// Explicit boundary defining which object IDs are permitted to be exported
// as part of an Event graph. References outside this boundary are
// refused explicitly.
class EventGraphExportBoundary {
 public:
  EventGraphExportBoundary() = default;

  EventGraphExportBoundary(std::initializer_list<ae::ObjId> ids) {
    for (auto id : ids) {
      if (id.is_valid()) {
        permitted_ids_.insert(id);
      }
    }
  }

  void Permit(ae::ObjId id) {
    if (id.is_valid()) {
      permitted_ids_.insert(id);
    }
  }

  void Permit(ae::Obj const& obj) { Permit(obj.obj_id); }

  template <typename T>
  void Permit(ae::ObjPtr<T> const& ptr) {
    if (ptr.is_valid()) {
      Permit(ptr.id());
    }
  }

  template <typename T, LinkScope Scope>
  void Permit(ObjectLink<T, Scope> const& link) {
    if (link.is_valid()) {
      Permit(link.id());
    }
  }

  bool IsPermitted(ae::ObjId id) const {
    return permitted_ids_.find(id) != permitted_ids_.end();
  }

  std::set<ae::ObjId> const& permitted_ids() const {
    return permitted_ids_;
  }

 private:
  std::set<ae::ObjId> permitted_ids_;
};

// Freeze an Event graph reachable from `event`.
// All reachable network-shared objects must be within `boundary`.
// If any reachable network-shared object is outside `boundary`, returns false
// explicitly.
// Leaves source state and source storage untouched.
// Produces a self-contained payload using native Aether serialization.
bool FreezeEventPayload(
    ae::Obj const& event,
    EventGraphExportBoundary const& boundary,
    std::vector<std::uint8_t>& out_payload);

// Overload for standalone/scalar Event: permits only `event.obj_id`.
// If `event` reaches any other network-shared object, returns false explicitly.
bool FreezeEventPayload(
    ae::Obj const& event,
    std::vector<std::uint8_t>& out_payload);

// Parse untrusted Event graph payload into scratch RamDomainStorage.
// Uses native Aether serialization. Returns false on malformed or truncated payload,
// or invalid root ID.
bool ParseEventPayload(
    std::vector<std::uint8_t> const& payload,
    ae::RamDomainStorage& parsed,
    ae::ObjId& out_root_id);

// Validate untrusted Event graph storage in disposable scratch.
// Checks:
// - complete object table and class chains;
// - root Event type derives from Event;
// - reference closure (all typed object references resolve within parsed);
// - reference type compatibility.
// Does NOT touch receiver objects or storage.
bool ValidateClosedEventGraphStorage(
    ae::RamDomainStorage const& parsed,
    ae::ObjId root_event_id,
    std::uint32_t expected_event_class_id = 0,
    std::vector<StoredClassChainInfo>* out_chains = nullptr);

// Import a validated closed Event graph into receiver Domain and storage.
// - Allocates unique receiver-local ObjIds avoiding any IDs occupied in
//   receiver Domain, receiver storage, or reserved by this import;
// - Exactly one mapping entry per included object;
// - Remaps all typed object references (including Node::base and aliases);
// - Preserves ordinary scalar values unchanged;
// - Excludes LocalPtr referents and leaves them empty;
// - Returns the imported Event in receiver Domain, ready for Apply or journal.
ae::Ptr<Event> ImportClosedEventGraph(
    ae::RamDomainStorage const& parsed,
    ae::ObjId root_event_id,
    ae::Domain& receiver_domain,
    ae::IDomainStorage& receiver_storage,
    std::set<ae::ObjId>& reserved_ids,
    std::map<ae::ObjId, ae::ObjId>* out_mapping = nullptr);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_NETWORK_GRAPH_H_
