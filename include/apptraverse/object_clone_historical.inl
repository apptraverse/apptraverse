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

}  // namespace detail

template <typename ConcreteNode>
bool BuildRemappedHistoricalBaseScratch(
    ConcreteNode& owner, ae::ObjId base_id,
    ae::RamDomainStorage const& captured_scratch,
    ae::RamDomainStorage& remapped_out,
    std::set<ae::ObjId>* live_reference_ids_out,
    ae::DomainGraph::CloneSaveRemapTable* capture_leaf_remap_out,
    std::set<ae::ObjId> const* owner_live_reference_ids) {
  if (live_reference_ids_out != nullptr) {
    live_reference_ids_out->clear();
  }
  if (capture_leaf_remap_out != nullptr) {
    capture_leaf_remap_out->clear();
  }
  ae::ObjId const owner_id = owner.obj_id;
  if (!owner_id.is_valid() || !base_id.is_valid() || owner.domain == nullptr) {
    return false;
  }

  auto& registry = ae::Registry::GetRegistry();
  auto const retains_live_identity_in_historical_base = [&](std::uint32_t cid) {
    if (registry.GenerationDistance(Node::kClassId, cid) >= 0) {
      return true;
    }
    if (registry.GenerationDistance(Event::kClassId, cid) >= 0) {
      return true;
    }
    return registry.GenerationDistance(Presenter::kClassId, cid) >= 0;
  };

  std::set<ae::ObjId> owner_live_refs;
  if (owner_live_reference_ids != nullptr) {
    owner_live_refs = *owner_live_reference_ids;
  } else {
    owner.CollectHistoricalCaptureLiveReferenceIds(owner_live_refs);
  }
  // A back-reference to the live owner is not a new snapshot leaf. Origins
  // are immutable external identities even when hydration has cached them.
  owner_live_refs.insert(owner_id);
  if (owner.origin_id().is_valid()) { owner_live_refs.insert(owner.origin_id()); }
  for (auto const& [id, _] : captured_scratch.state) {
    if (auto live = owner.domain->Find(id); live && live->origin_id().is_valid()) {
      owner_live_refs.insert(live->origin_id());
    }
  }
  if (live_reference_ids_out != nullptr) { *live_reference_ids_out = owner_live_refs; }

  std::set<ae::ObjId> leaf_ids;
  for (auto const& [object_id, class_map_opt] : captured_scratch.state) {
    if (!class_map_opt || object_id.id() == base_id.id() ||
        object_id.id() == owner_id.id()) {
      continue;
    }
    leaf_ids.insert(object_id);
  }

  detail::CloneRemap remap;
  remap[base_id.id()] = base_id.id();
  for (ae::ObjId const old_id : leaf_ids) {
    bool retain_identity = owner_live_refs.count(old_id) != 0;
    for (auto const& [class_id, _] : *captured_scratch.state.at(old_id)) {
      retain_identity = retain_identity ||
          retains_live_identity_in_historical_base(class_id);
    }
    if (retain_identity) {
      if (live_reference_ids_out != nullptr) {
        live_reference_ids_out->insert(old_id);
      }
      continue;
    }
    ae::ObjId const allocated = detail::AllocateCloneObjIdForHistorical(
        owner_id.id(), leaf_ids, remap, *owner.domain);
    if (!allocated.is_valid()) {
      return false;
    }
    remap[old_id.id()] = allocated.id();
  }

  bool any_leaf_remap = false;
  for (auto const& [remap_key, _] : remap) {
    if (remap_key != base_id.id()) {
      any_leaf_remap = true;
      break;
    }
  }

  remapped_out = {};

  if (!any_leaf_remap) {
    remapped_out = captured_scratch;
    if (capture_leaf_remap_out != nullptr) {
      *capture_leaf_remap_out = remap;
    }
    return true;
  }

  ae::RamDomainStorage scratch;
  ae::Domain scratch_domain{scratch};
  ae::DomainGraph save_graph{
      &scratch_domain, ae::GraphSerializationScope::HistoricalBaseSnapshot};
  save_graph.force_materialize = true;
  save_graph.SetCloneSaveRemap(&remap);

  if (!save_graph.cycle_detector.Add(ConcreteNode::kClassId, owner_id)) {
    return false;
  }
  auto const save_result = save_graph.Save(owner, base_id);
  if (!save_result || !save_result->IsOk()) {
    return false;
  }

  remapped_out = std::move(scratch);
  if (capture_leaf_remap_out != nullptr) {
    *capture_leaf_remap_out = remap;
  }
  return true;
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
