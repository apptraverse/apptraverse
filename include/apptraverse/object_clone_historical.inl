#ifndef APPTRAVERSE_OBJECT_CLONE_HISTORICAL_INL_
#define APPTRAVERSE_OBJECT_CLONE_HISTORICAL_INL_

#include <unordered_map>

#include "aether-objects/obj/registry.h"

#include "apptraverse/event.h"
#include "apptraverse/node.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/presenter.h"

namespace apptraverse {
namespace detail {

using CloneRemap = ae::DomainGraph::CloneSaveRemapTable;

ae::ObjId AllocateCloneObjIdForHistorical(
    ae::ObjId::Type source_root, std::set<ae::ObjId> const& copy_set,
    CloneRemap const& remap, ae::Domain& target);

bool InjectLayeredObjects(ae::RamDomainStorage const& scratch,
                          ae::IDomainStorage& storage);

template <typename ConcreteNode>
HistoricalNodeSerializer HistoricalSerializerFor() {
  return {ConcreteNode::kClassId,
          [](ae::DomainGraph& graph, Node& object, ae::ObjId storage_id) {
            return graph.Save(static_cast<ConcreteNode&>(object), storage_id);
          }};
}

}  // namespace detail

template <typename ConcreteNode>
bool BuildRemappedHistoricalBaseScratch(
    ConcreteNode& owner, ae::ObjId base_id,
    ae::RamDomainStorage const& captured_scratch,
    ae::RamDomainStorage& remapped_out,
    std::set<ae::ObjId>* live_reference_ids_out,
    ae::DomainGraph::CloneSaveRemapTable* capture_leaf_remap_out,
    std::set<ae::ObjId> const* owner_live_reference_ids) {
  return detail::BuildRemappedHistoricalBaseScratch(
      owner, detail::HistoricalSerializerFor<ConcreteNode>(), base_id,
      captured_scratch, remapped_out, live_reference_ids_out,
      capture_leaf_remap_out, owner_live_reference_ids);
}

template <typename ConcreteNode>
bool InjectRemappedHistoricalBaseSnapshot(
    ConcreteNode& owner, ae::ObjId base_id,
    ae::RamDomainStorage const& captured_scratch,
    ae::IDomainStorage& persistent_storage) {
  ae::RamDomainStorage remapped;
  if (!BuildRemappedHistoricalBaseScratch(owner, base_id, captured_scratch,
                                          remapped, nullptr, nullptr,
                                          nullptr)) {
    return false;
  }
  return detail::InjectLayeredObjects(remapped, persistent_storage);
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_OBJECT_CLONE_HISTORICAL_INL_
