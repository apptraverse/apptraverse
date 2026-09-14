#ifndef APPTRAVERSE_SHARED_NETWORK_GRAPH_H_
#define APPTRAVERSE_SHARED_NETWORK_GRAPH_H_

#include <cstdint>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/idomain_storage.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/shared_node.h"

namespace apptraverse {

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

// Same network-shared view, as transportable bytes. Object, class, and version
// identities are preserved, so a replica that imports the payload keeps the
// sender's ObjIds and Share relationship identities.
std::vector<std::uint8_t> SerializeNetworkSharedObjectGraph(
    ae::Obj const& root);

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

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_NETWORK_GRAPH_H_
