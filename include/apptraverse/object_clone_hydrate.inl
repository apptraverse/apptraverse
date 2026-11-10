#ifndef APPTRAVERSE_OBJECT_CLONE_HYDRATE_INL_
#define APPTRAVERSE_OBJECT_CLONE_HYDRATE_INL_

#include "apptraverse/overlay_domain_storage.h"

namespace apptraverse {

bool InjectAllLayeredObjects(ae::RamDomainStorage const& scratch,
                             ae::IDomainStorage& storage);

bool SeedLiveReferenceLayersInOverlay(
    ae::Domain& target, ae::RamDomainStorage& overlay_layers,
    ae::RamDomainStorage const& layered_scratch,
    std::set<ae::ObjId> const& live_reference_ids);

bool RemapLiveObjectGraph(
    ae::Domain& target, ae::Ptr<ae::Obj> const& root,
    std::set<ae::ObjId> const& boundary_ids,
    std::set<ae::ObjId> const& canonical_shared_ids,
    std::map<ae::ObjId, ae::Ptr<ae::Obj>> const& extra_live,
    std::map<ae::ObjId, ae::ObjId> const* id_remap);

// Loads layered_scratch bytes for storage_id into live object using T::kClassId.
template <typename ConcreteObj>
bool HydrateLiveObjectFromLayerScratch(
    ae::Domain& target, ConcreteObj& object, ae::ObjId storage_id,
    ae::RamDomainStorage const& layered_scratch,
    std::set<ae::ObjId> const& boundary_ids,
    std::set<ae::ObjId> const& live_reference_ids, bool rebind_live_graph,
    std::map<ae::ObjId, ae::ObjId> const* loaded_to_live_id_remap) {
  if (!storage_id.is_valid()) {
    return false;
  }
  std::set<ae::ObjId> hydrate_supply{boundary_ids.begin(), boundary_ids.end()};
  OverlayDomainStorage materialize_overlay{target.domain_storage(),
                                           hydrate_supply};
  if (!InjectAllLayeredObjects(layered_scratch, materialize_overlay.overlay())) {
    return false;
  }
  if (!SeedLiveReferenceLayersInOverlay(
          target, materialize_overlay.overlay(), layered_scratch,
          live_reference_ids)) {
    return false;
  }
  OverlayDomainStorage read_overlay{materialize_overlay.overlay(),
                                    target.domain_storage(), hydrate_supply};
  ae::Domain read_domain{read_overlay};
  read_domain.set_parent_domain(&target);
  ae::DomainGraph load_graph{&read_domain};
  std::optional<ae::seri::SeriResult> const load_result =
      load_graph.Load(object, storage_id);
  if (!load_result.has_value() || !load_result->IsOk()) {
    return false;
  }
  ae::Ptr<ae::Obj> const root = target.Find(object.obj_id);
  if (!root) {
    return false;
  }
  object.domain = &target;
  if (rebind_live_graph) {
    std::map<ae::ObjId, ae::Ptr<ae::Obj>> extra_live;
    std::set<ae::ObjId> canonical_shared;
    for (ae::ObjId const id : live_reference_ids) {
      if (ae::Ptr<ae::Obj> const obj = target.Find(id)) {
        extra_live[id] = obj;
      }
    }
    if (loaded_to_live_id_remap != nullptr) {
      for (auto const& [_, live_id] : *loaded_to_live_id_remap) {
        if (ae::Ptr<ae::Obj> const obj = target.Find(live_id)) {
          extra_live[live_id] = obj;
        }
      }
    }
    if (!RemapLiveObjectGraph(target, root, boundary_ids, canonical_shared,
                              extra_live, loaded_to_live_id_remap)) {
      return false;
    }
  }
  return true;
}

template <typename ConcreteObj>
bool HydrateLiveObjectFromLayerScratch(
    ae::Domain& target, ConcreteObj& object, ae::ObjId storage_id,
    ae::RamDomainStorage const& layered_scratch,
    std::set<ae::ObjId> const& boundary_ids,
    std::set<ae::ObjId> const& live_reference_ids) {
  return HydrateLiveObjectFromLayerScratch(target, object, storage_id,
                                         layered_scratch, boundary_ids,
                                         live_reference_ids, false, nullptr);
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_OBJECT_CLONE_HYDRATE_INL_
