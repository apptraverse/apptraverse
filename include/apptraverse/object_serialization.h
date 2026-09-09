#ifndef APPTRAVERSE_OBJECT_SERIALIZATION_H_
#define APPTRAVERSE_OBJECT_SERIALIZATION_H_

#include <cstdint>
#include <vector>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/idomain_storage.h"
#include "aether-objects/obj/obj.h"
#include "aether-objects/ptr/ptr.h"

#include "apptraverse/node.h"
#include "apptraverse/presenter.h"
#include "apptraverse/publication_channel.h"

namespace apptraverse {

// Future optimization: serialize reflected concrete state without reflected base
// class for UI publication, so Node::base/journal do not enter the buffer.
// TODO(surfaces): structural publication bandwidth / delta protocol — not this
// slice; live presentation correctness does not require a smaller payload yet.

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

// Reachability for presentation: omits Node::base and Node::journal so
// historical Event-held objects are not treated as live UI topology.
void CollectLiveReachableObjects(ae::Obj& root, std::vector<ae::Obj*>& out);

// GUI-thread presentation phase. Walks live topology from the GUI root and
// calls Presenter::OnLoad for each Presenter that is not yet
// presentation_loaded and reports ReadyForPresentation(). Object Load must
// not call this. Safe to call again after structural publication so only
// newly introduced presenters activate.
void InitializePresenters(ae::Obj& gui_root, void* host = nullptr);

// Same activation rule as InitializePresenters. Prefer this name at
// incremental-apply sites; both may be used interchangeably.
void InitializeNewPresenters(ae::Obj& gui_root, void* host = nullptr);

// Inverse of InitializePresenters. Unloads by descending presentation_load_order
// (children before parents). Object destruction does not call this.
void UnloadPresenters(ae::Obj& gui_root);

// After a structural publication is fully applied: OnUnload presenters that
// were active but are no longer in live topology (descending load order), then
// OnLoad any new live presenters. previously_active owns Presenter::ptr so
// OnUnload runs while the objects still exist.
void UpdatePresentersAfterStructuralPublication(
    ae::Obj& gui_root, std::vector<Presenter::ptr> const& previously_active,
    void* host = nullptr);

// Capture live ObjPtr anchors and active Presenter::ptr before a structural
// apply. Keeps survivor C++ identity and removed presenters alive across apply.
struct StructuralPresentationKeepalive {
  std::vector<ae::Ptr<ae::Obj>> live_objects;
  std::vector<Presenter::ptr> active_presenters;
};

StructuralPresentationKeepalive CaptureStructuralPresentationKeepalive(
    ae::Obj& gui_root);

// Apply structural envelope under generic live keepalive, then update
// presenters (unload removed by load-order, OnLoad new). Prefer this over
// calling ApplyStructuralPublication + UpdatePresenters separately.
ae::Obj& ApplyStructuralPublicationAndUpdatePresenters(
    ByteSource& in, ae::Domain& domain, ae::IDomainStorage& storage,
    ae::Obj& gui_root, void* host = nullptr);

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
// call presenter hooks — prefer ApplyStructuralPublicationAndUpdatePresenters
// when presentation must stay in sync.
ae::Obj& ApplyStructuralPublication(ByteSource& in, ae::Domain& domain,
                                    ae::IDomainStorage& storage);

}  // namespace apptraverse

#endif  // APPTRAVERSE_OBJECT_SERIALIZATION_H_
