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
// root and calls Presenter::OnLoad for each Presenter that is not yet
// presentation_loaded and reports ReadyForPresentation(). Object Load must
// not call this. Safe to call again after structural publication so only
// newly introduced presenters activate.
void InitializePresenters(ae::Obj& gui_root, void* host = nullptr);

// Same activation rule as InitializePresenters. Prefer this name at
// incremental-apply sites; both may be used interchangeably.
void InitializeNewPresenters(ae::Obj& gui_root, void* host = nullptr);

// Inverse of InitializePresenters. Call only for a GUI graph that completed
// that pass. Object destruction does not call this.
void UnloadPresenters(ae::Obj& gui_root);

void FinalizeUiNodeState(ae::Obj& object, std::uint64_t generation);

// PUBLISH uses SerializeObjectToBuffer, which writes a temporary
// RamDomainStorage scratch, not DirectoryDomainStorage.
// One changed Node for an incremental GUI publication. Envelope:
// object id, generation, payload length, SerializeObjectToBuffer payload.
//
// The MainWindow layer written here is an ordinary object Save, so it still
// contains Node base/journal references. Referenced Event object layers are
// not copied into this publication. After deserialize, FinalizeUiNodeState
// clears the GUI node's base and journal. A future optimization may omit
// Node bookkeeping entirely; that is not done here.
void SerializeIncrementalNodePublication(Node const& node, ByteSink& out);

// Apply that envelope into the already-materialized GUI object. Does not
// create a Domain, does not call presenter hooks.
ae::Obj& ApplyIncrementalPublication(ByteSource& in, ae::Domain& domain,
                                     ae::IDomainStorage& storage);

// Structural incremental publication: one changed Node including newly
// referenced objects (children, presenters). Envelope: object id, generation,
// payload length, SerializeObjectGraphToBuffer payload. Use this when the
// Node's reachable graph grows (e.g. Add Item). Field-only updates of an
// already-mirrored object may keep SerializeIncrementalNodePublication.
void SerializeStructuralNodePublication(Node const& node, ByteSink& out);

// Apply a structural envelope into an already-mirrored GUI Node. Nested
// LoadRoot materializes new shells for newly referenced ObjIds. Does not
// call presenter hooks — caller runs InitializeNewPresenters after apply.
ae::Obj& ApplyStructuralPublication(ByteSource& in, ae::Domain& domain,
                                    ae::IDomainStorage& storage);

}  // namespace apptraverse

#endif  // APPTRAVERSE_OBJECT_SERIALIZATION_H_
