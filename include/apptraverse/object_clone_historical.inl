#ifndef APPTRAVERSE_OBJECT_CLONE_HISTORICAL_INL_
#define APPTRAVERSE_OBJECT_CLONE_HISTORICAL_INL_

#include <deque>
#include <unordered_map>

#include "aether-objects/obj/registry.h"

#include "apptraverse/event.h"
#include "apptraverse/node.h"
#include "apptraverse/presenter.h"

namespace apptraverse {
namespace detail {

using CloneRemap = ae::DomainGraph::CloneSaveRemapTable;

ae::ObjId AllocateCloneObjIdForHistorical(
    ae::ObjId::Type source_root, std::set<ae::ObjId> const& copy_set,
    CloneRemap const& remap, ae::Domain& target);

void EnqueueCloneReferenceForHistorical(void* context, ae::ObjId id);

bool InjectLayeredObjects(ae::RamDomainStorage const& scratch,
                          ae::IDomainStorage& storage);

}  // namespace detail

template <typename ConcreteNode>
bool InjectRemappedHistoricalBaseSnapshot(
    ConcreteNode& owner, ae::ObjId base_id,
    ae::RamDomainStorage const& captured_scratch,
    ae::IDomainStorage& persistent_storage) {
  ae::ObjId const owner_id = owner.obj_id;
  if (!owner_id.is_valid() || !base_id.is_valid() || owner.domain == nullptr) {
    return false;
  }

  auto& registry = ae::Registry::GetRegistry();
  auto const retains_live_identity_in_historical_base = [&](ae::Obj const& obj) {
    if (registry.GenerationDistance(Node::kClassId, obj.GetClassId()) >= 0) {
      return true;
    }
    if (registry.GenerationDistance(Event::kClassId, obj.GetClassId()) >= 0) {
      return true;
    }
    // Presenter is not journaled model state; historical base keeps distilled
    // live presenter identity and binding graph (see contract headless replay).
    return registry.GenerationDistance(Presenter::kClassId, obj.GetClassId()) >=
           0;
  };

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
  ae::Domain live_domain{owner.domain->domain_storage()};
  for (ae::ObjId const old_id : leaf_ids) {
    ae::Ptr<ae::Obj> obj = owner.domain->Find(old_id);
    if (!obj) {
      ae::DomainGraph load_graph{owner.domain};
      ae::Ptr<ae::Obj> loaded;
      if (!load_graph.LoadRoot(old_id, loaded).IsOk() || !loaded) {
        continue;
      }
      obj = loaded;
    }
    if (retains_live_identity_in_historical_base(*obj)) {
      continue;
    }
    ae::ObjId const allocated = detail::AllocateCloneObjIdForHistorical(
        owner_id.id(), leaf_ids, remap, live_domain);
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

  if (!any_leaf_remap) {
    auto const base_entry = captured_scratch.state.find(base_id);
    if (base_entry == captured_scratch.state.end() ||
        !base_entry->second.has_value()) {
      return false;
    }
    for (auto const& [class_id, versions] : *base_entry->second) {
      for (auto const& [version, data] : versions) {
        auto writer =
            persistent_storage.Store(ae::DomainQuery{base_id, class_id, version});
        if (writer == nullptr) {
          return false;
        }
        if (!data.empty() &&
            !writer->Write(ae::seri::DataWriteTag{data.data(), data.size()})) {
          return false;
        }
      }
    }
    return true;
  }

  ae::RamDomainStorage scratch;
  ae::Domain scratch_domain{scratch};
  ae::DomainGraph save_graph{
      &scratch_domain, ae::GraphSerializationScope::HistoricalBaseSnapshot};
  save_graph.force_materialize = true;
  save_graph.SetCloneSaveRemap(&remap);

  std::deque<ae::ObjId> queue;
  std::set<ae::ObjId::Type> seen;
  struct RefCtx {
    std::deque<ae::ObjId>* queue;
    std::set<ae::ObjId::Type>* seen;
  };
  RefCtx ref_ctx{&queue, &seen};
  save_graph.SetCloneReferenceVisitor(
      &ref_ctx, detail::EnqueueCloneReferenceForHistorical);

  if (!save_graph.cycle_detector.Add(ConcreteNode::kClassId, owner_id)) {
    return false;
  }
  auto const save_result = save_graph.Save(owner, base_id);
  if (!save_result || !save_result->IsOk()) {
    return false;
  }

  return detail::InjectLayeredObjects(scratch, persistent_storage);
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_OBJECT_CLONE_HISTORICAL_INL_
