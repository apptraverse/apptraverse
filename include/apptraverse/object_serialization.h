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

// Full-graph initial publication: root ObjId then SerializeObjectGraphToBuffer.
// Load creates UI shells from the buffer (no model Domain / model Obj*).
void SerializeInitialPublication(ae::Obj const& root, ByteSink& out);
ae::Ptr<ae::Obj> LoadInitialPublication(ByteSource& in, ae::Domain& ui_domain,
                                        ae::IDomainStorage& ui_storage);

void CollectReachableObjects(ae::Obj& root, std::vector<ae::Obj*>& out);
void CollectReachableNodes(ae::Obj& root, std::vector<Node*>& out);

// GUI-thread presentation phase. Walks reachable live objects from the GUI
// root and calls Presenter::OnLoad for each Presenter. Object Load must not
// call this. The pass is invoked once per GUI mirror.
void InitializePresenters(ae::Obj& gui_root, void* host = nullptr);

// Inverse of InitializePresenters. Call only for a GUI graph that completed
// that pass. Object destruction does not call this.
void UnloadPresenters(ae::Obj& gui_root);

void FinalizeUiNodeState(ae::Obj& object, std::uint64_t generation);

// One changed Node for an incremental GUI publication. Envelope:
// object id, generation, payload length, SerializeObjectToBuffer payload.
// Does not include referenced object layers (presenter stays the existing
// instance; journal events are not copied into the GUI Domain).
void SerializeIncrementalNodePublication(Node const& node, ByteSink& out);

// Apply that envelope into the already-materialized GUI object. Does not
// create a Domain, does not call presenter hooks.
ae::Obj& ApplyIncrementalPublication(ByteSource& in, ae::Domain& domain,
                                     ae::IDomainStorage& storage);

}  // namespace apptraverse

#endif  // APPTRAVERSE_OBJECT_SERIALIZATION_H_
