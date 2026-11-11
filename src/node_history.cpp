#include "apptraverse/node.h"

#include "aether-objects/obj/registry.h"
#include "apptraverse/event.h"
#include "apptraverse/presenter.h"

namespace apptraverse {

namespace detail {

bool BuildRemappedHistoricalBaseScratch(
    Node& owner, HistoricalNodeSerializer serializer, ae::ObjId base_id,
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

  CloneRemap remap;
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
    ae::ObjId const allocated = AllocateCloneObjIdForHistorical(
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

  if (!save_graph.cycle_detector.Add(serializer.class_id, owner_id)) {
    return false;
  }
  auto const save_result = serializer.save(save_graph, owner, base_id);
  if (!save_result || !save_result->IsOk()) {
    return false;
  }

  remapped_out = std::move(scratch);
  if (capture_leaf_remap_out != nullptr) {
    *capture_leaf_remap_out = remap;
  }
  return true;
}

}  // namespace detail

bool Node::TryRebuildFromBaseAndReplay(
    Node& target, detail::HistoricalNodeSerializer serializer,
    void* rebuild_scratch) {
  auto owner_id = obj_id;
  auto saved_base = base;
  auto saved_journal = journal;
  target.StashLocalPersistentAcrossRebuild(rebuild_scratch);
  ae::Ptr<ae::Obj> const base_obj = saved_base.Load();
  if (!base_obj) {
    obj_id = owner_id;
    base = saved_base;
    journal = std::move(saved_journal);
    target.RestoreLocalPersistentAcrossRebuild(rebuild_scratch);
    return false;
  }
  auto& base_node = static_cast<Node&>(*base_obj);
  base_node.PrepareReachableMutableLeavesForHistoricalCapture();
  std::set<ae::ObjId> base_live_refs;
  base_node.CollectHistoricalCaptureLiveReferenceIds(base_live_refs);
  ae::RamDomainStorage base_layers;
  {
    ae::Domain layer_domain{base_layers};
    ae::DomainGraph save_graph{
        &layer_domain, ae::GraphSerializationScope::HistoricalBaseSnapshot};
    save_graph.force_materialize = true;
    auto const save_result = serializer.save(save_graph, base_node, owner_id);
    if (!save_result.has_value() || !save_result->IsOk()) {
      obj_id = owner_id;
      base = saved_base;
      journal = std::move(saved_journal);
      target.RestoreLocalPersistentAcrossRebuild(rebuild_scratch);
      return false;
    }
  }
  // Replay writes into a private copy of mutable leaves. Reusing the base's
  // IDs would make replayed Events mutate the historical snapshot itself.
  ae::RamDomainStorage replay_layers;
  std::set<ae::ObjId> live_references;
  if (!detail::BuildRemappedHistoricalBaseScratch(
          base_node, serializer, owner_id, base_layers, replay_layers,
          &live_references, nullptr, &base_live_refs) ||
      !HydrateLiveObjectFromLayerScratch(*domain, target, owner_id,
                                        replay_layers, live_references)) {
    obj_id = owner_id;
    base = saved_base;
    journal = std::move(saved_journal);
    target.RestoreLocalPersistentAcrossRebuild(rebuild_scratch);
    return false;
  }
  obj_id = owner_id;
  base = saved_base;
  journal = std::move(saved_journal);
  target.RestoreLocalPersistentAcrossRebuild(rebuild_scratch);
  generation_ = 1;
  return TryReplayJournal();
}

void Node::CaptureBaseStateInto(
    Node& target, detail::HistoricalNodeSerializer serializer) {
  assert(domain != nullptr);
  assert(base.is_valid());
  assert(base.is_loaded());
  assert(journal.empty());
  assert(base.id() != obj_id);

  auto owner_id = obj_id;
  auto saved_base = base;
  auto saved_journal = journal;

  // Lazy live leaves may need the previous base to reconstruct changes that
  // compaction has removed from the journal but has not persisted yet.
  target.PrepareReachableMutableLeavesForHistoricalCapture();
  base = Node::ptr{};
  journal.clear();
  std::set<ae::ObjId> owner_live_refs;
  target.CollectHistoricalCaptureLiveReferenceIds(owner_live_refs);

  ae::RamDomainStorage scratch;
  ae::Domain scratch_domain{scratch};
  ae::DomainGraph save_graph{
      &scratch_domain,
      ae::GraphSerializationScope::HistoricalBaseSnapshot};
  save_graph.force_materialize = true;
  bool const owner_marked =
      save_graph.cycle_detector.Add(serializer.class_id, owner_id);
  assert(owner_marked);
  (void)owner_marked;
  auto const capture_save = serializer.save(save_graph, target, saved_base.id());
  if (!capture_save.has_value() || !capture_save->IsOk()) {
    obj_id = owner_id;
    base = saved_base;
    journal = std::move(saved_journal);
    std::fprintf(stderr, "Historical capture failed at save (owner=%lu base=%lu)\n",
                 static_cast<unsigned long>(owner_id.id()),
                 static_cast<unsigned long>(saved_base.id().id()));
    std::abort();
  }

  ae::RamDomainStorage historical_layers;
  std::set<ae::ObjId> live_references;
  if (!detail::BuildRemappedHistoricalBaseScratch(
          target, serializer, saved_base.id(), scratch, historical_layers,
          &live_references, nullptr, &owner_live_refs)) {
    obj_id = owner_id;
    base = saved_base;
    journal = std::move(saved_journal);
    std::fprintf(stderr, "Historical capture failed at remap (owner=%lu base=%lu)\n",
                 static_cast<unsigned long>(owner_id.id()),
                 static_cast<unsigned long>(saved_base.id().id()));
    std::abort();
  }
  ae::Ptr<ae::Obj> const base_live = domain->Find(saved_base.id());
  if (!base_live ||
      !HydrateLiveObjectFromLayerScratch(*domain, *base_live, saved_base.id(),
                                        historical_layers, live_references)) {
    obj_id = owner_id;
    base = saved_base;
    journal = std::move(saved_journal);
    std::fprintf(stderr, "Historical capture failed at hydrate (owner=%lu base=%lu)\n",
                 static_cast<unsigned long>(owner_id.id()),
                 static_cast<unsigned long>(saved_base.id().id()));
    std::abort();
  }

  obj_id = owner_id;
  base = saved_base;
  journal = std::move(saved_journal);
  history_base_recaptured_ = true;
}

}  // namespace apptraverse
