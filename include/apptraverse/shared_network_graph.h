#ifndef APPTRAVERSE_SHARED_NETWORK_GRAPH_H_
#define APPTRAVERSE_SHARED_NETWORK_GRAPH_H_

#include <cstdint>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/idomain_storage.h"
#include "aether-objects/obj/obj.h"

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

// Serialize any RamDomainStorage into the canonical wire representation.
std::vector<std::uint8_t> SerializeRamDomainStorage(
    ae::RamDomainStorage const& storage);

// Same network-shared view, as transportable bytes. Object, class, and version
// identities are preserved, so a replica that imports the payload keeps the
// sender's ObjIds and Share relationship identities.
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

// Specialization for standalone Event payloads:
// Exactly one object (under kStandaloneEventScratchId), its most-derived class
// matches expected_event_class_id, derives from Event, and the class layer
// is actually present in storage.
bool ValidateStandaloneEventStorage(
    ae::RamDomainStorage const& parsed,
    std::uint32_t expected_event_class_id);

// Parse an untrusted payload into an in-memory graph. Nothing outside parsed
// is touched, so a caller can inspect the result before deciding whether the
// replica accepts it. Returns false for malformed or truncated input.
bool ParseObjectGraphPayload(std::vector<std::uint8_t> const& payload,
                             ae::RamDomainStorage& parsed);

// Write an already parsed graph into a replica's own storage.
void CommitObjectGraph(ae::RamDomainStorage const& parsed,
                       ae::IDomainStorage& target_storage);

// Parse, then commit. Malformed input leaves target_storage untouched: the
// whole payload is validated before the first write.
bool ImportObjectGraphPayload(std::vector<std::uint8_t> const& payload,
                              ae::IDomainStorage& target_storage);

// V1 standalone Event: the reachable network-shared graph is the Event root
// and nothing else. The wire form is that object's class/version layers and
// does not make the sender Event ObjId a receiver storage key.
//
// Scratch objects used while parsing live under kStandaloneEventScratchId in
// the caller's RamDomainStorage, never in production storage.
inline constexpr ae::ObjId kStandaloneEventScratchId{1};

bool FreezeStandaloneEventPayload(ae::Obj const& event,
                                  std::vector<std::uint8_t>& out);

bool ParseStandaloneEventPayload(std::vector<std::uint8_t> const& payload,
                                 ae::RamDomainStorage& parsed);

// Copy one scratch object's class layers into target storage under local_id.
void CommitStandaloneEventObject(ae::RamDomainStorage const& parsed,
                                 ae::ObjId local_id,
                                 ae::IDomainStorage& target_storage);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_NETWORK_GRAPH_H_
