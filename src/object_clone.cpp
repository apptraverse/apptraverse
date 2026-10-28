#include "apptraverse/object_clone.h"

#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/registry.h"

#include "apptraverse/node.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse {
namespace {

bool InjectLayerBytes(ae::IDomainStorage& storage, ae::DomainQuery const& query,
                      ae::RamDomainStorage::Data const& data) {
  auto writer = storage.Store(query);
  if (!writer) {
    return false;
  }
  return static_cast<bool>(writer->Write(
      ae::seri::DataWriteTag{data.data(), data.size()}));
}

bool InjectClassData(ae::RamDomainStorage::ClassData const& class_data,
                     ae::ObjId dest_id, ae::IDomainStorage& storage) {
  for (auto const& [class_id, versions] : class_data) {
    for (auto const& [version, data] : versions) {
      if (!InjectLayerBytes(storage, {dest_id, class_id, version}, data)) {
        return false;
      }
    }
  }
  return true;
}

bool InjectAllLayeredObjects(ae::RamDomainStorage const& scratch,
                             ae::IDomainStorage& storage) {
  for (auto const& [object_id, class_map_opt] : scratch.state) {
    if (!class_map_opt || class_map_opt->empty()) {
      continue;
    }
    if (!InjectClassData(*class_map_opt, object_id, storage)) {
      return false;
    }
  }
  return true;
}

bool ObjectIdOccupiedInStorage(ae::IDomainStorage& storage, ae::ObjId id) {
  return !storage.Enumerate(id).empty();
}

bool ObjectIdOccupiedInDomain(ae::Domain& domain, ae::ObjId id) {
  return static_cast<bool>(domain.Find(id)) ||
         ObjectIdOccupiedInStorage(domain.domain_storage(), id);
}

CloneCanonicalResult Fail(CloneError error) {
  return CloneCanonicalResult{{}, error};
}

using CloneRemap = ae::DomainGraph::CloneSaveRemapTable;

bool RemapIdReserved(ae::ObjId::Type candidate, ae::ObjId::Type source_root,
                     std::set<ae::ObjId> const& copy_set,
                     std::set<ae::ObjId> const& boundary_ids,
                     CloneRemap const& remap, ae::Domain& target) {
  if (candidate == 0u) {
    return true;
  }
  if (candidate == source_root) {
    return true;
  }
  for (ae::ObjId const id : copy_set) {
    if (id.id() == candidate) {
      return true;
    }
  }
  for (ae::ObjId const id : boundary_ids) {
    if (id.id() == candidate) {
      return true;
    }
  }
  for (auto const& [_, mapped] : remap) {
    if (mapped == candidate) {
      return true;
    }
  }
  return ObjectIdOccupiedInDomain(target, ae::ObjId{candidate});
}

ae::ObjId AllocateCloneObjId(ae::ObjId::Type source_root,
                             std::set<ae::ObjId> const& copy_set,
                             std::set<ae::ObjId> const& boundary_ids,
                             CloneRemap const& remap, ae::Domain& target) {
  for (int attempt = 0; attempt < 64; ++attempt) {
    ae::ObjId const candidate = ae::ObjId::GenerateUnique();
    if (!RemapIdReserved(candidate.id(), source_root, copy_set, boundary_ids,
                         remap, target)) {
      return candidate;
    }
  }
  return ae::ObjId{};
}

struct CloneReferenceQueueContext {
  std::deque<ae::ObjId>* queue;
  std::set<ae::ObjId::Type>* seen;
  std::set<ae::ObjId> const* boundary_ids;
  ae::ObjId stable_origin;
  ae::ObjId const* current_origin;
};

void EnqueueCloneReference(void* context, ae::ObjId id) {
  auto* ctx = static_cast<CloneReferenceQueueContext*>(context);
  if (!id.is_valid()) {
    return;
  }
  if (ctx->boundary_ids->count(id) != 0) {
    return;
  }
  if (id == ctx->stable_origin) {
    return;
  }
  if (ctx->current_origin != nullptr && id == *ctx->current_origin) {
    return;
  }
  if (ctx->seen->insert(id.id()).second) {
    ctx->queue->push_back(id);
  }
}

ae::Ptr<ae::Obj> ResolveSnapshotObject(
    ae::Domain& source, ae::ObjId id, ae::Domain& storage_reader,
    std::vector<ae::Ptr<ae::Obj>>& storage_keepalive) {
  if (ae::Ptr<ae::Obj> live = source.Find(id)) {
    return live;
  }
  ae::DomainGraph load_graph{&storage_reader};
  ae::Ptr<ae::Obj> loaded;
  if (!load_graph.LoadRoot(id, loaded).IsOk() || !loaded) {
    return {};
  }
  storage_keepalive.push_back(loaded);
  return loaded;
}

bool SaveCanonicalSnapshotObject(ae::Ptr<ae::Obj> const& object,
                                 ae::RamDomainStorage& snapshot,
                                 CloneReferenceQueueContext& ref_ctx) {
  if (!object) {
    return false;
  }
  ae::Domain snapshot_domain{snapshot};
  ae::DomainGraph graph{&snapshot_domain,
                        ae::GraphSerializationScope::CanonicalClone};
  CloneRemap identity_remap{{object->obj_id.id(), object->obj_id.id()}};
  graph.SetCloneSaveRemap(&identity_remap);
  ae::ObjId const origin_id = object->origin_id();
  ref_ctx.current_origin = origin_id.is_valid() ? &origin_id : nullptr;
  graph.SetCloneReferenceVisitor(&ref_ctx, EnqueueCloneReference);
  auto* factory =
      ae::Registry::GetRegistry().FindFactory(object->GetClassId());
  if (factory == nullptr || factory->save == nullptr) {
    return false;
  }
  std::optional<ae::seri::SeriResult> const save_result =
      factory->save(&graph, object, object->obj_id);
  return save_result.has_value() && save_result->IsOk();
}

bool BuildCanonicalSnapshot(ae::Domain& source, ae::ObjId source_id,
                            CloneOptions const& options,
                            ae::RamDomainStorage& snapshot,
                            std::set<ae::ObjId>& copy_set) {
  std::deque<ae::ObjId> queue;
  std::set<ae::ObjId::Type> seen;
  queue.push_back(source_id);
  seen.insert(source_id.id());

  ae::Domain storage_reader{source.domain_storage()};
  std::vector<ae::Ptr<ae::Obj>> storage_keepalive;

  CloneReferenceQueueContext ref_ctx{&queue, &seen, &options.boundary_ids,
                                   options.stable_origin, nullptr};

  while (!queue.empty()) {
    ae::ObjId const id = queue.front();
    queue.pop_front();
    if (id != source_id && options.boundary_ids.count(id) != 0) {
      continue;
    }
    ae::Ptr<ae::Obj> object = ResolveSnapshotObject(source, id, storage_reader,
                                                    storage_keepalive);
    if (!object) {
      return false;
    }
    if (!SaveCanonicalSnapshotObject(object, snapshot, ref_ctx)) {
      return false;
    }
    copy_set.insert(id);
  }

  return copy_set.find(source_id) != copy_set.end();
}

Node* AsNode(ae::Obj* obj) {
  if (obj == nullptr) {
    return nullptr;
  }
  if (ae::Registry::GetRegistry().GenerationDistance(Node::kClassId,
                                                     obj->GetClassId()) < 0) {
    return nullptr;
  }
  return static_cast<Node*>(obj);
}

bool InitializeFreshHistoryForClonedNodes(
    std::vector<ae::Ptr<ae::Obj>> const& cloned_copies,
    std::vector<ae::ObjId::Type>& fresh_base_ids) {
  for (ae::Ptr<ae::Obj> const& obj : cloned_copies) {
    Node* node = AsNode(obj.get());
    if (node == nullptr) {
      continue;
    }
    if (node->base.is_valid() || !node->journal.empty()) {
      return false;
    }
    InitializeRuntimeNode(*node);
    if (!node->base.is_valid() || !node->base.is_loaded()) {
      return false;
    }
    node->base.Load()->SetOriginId(node->origin_id());
    fresh_base_ids.push_back(node->base.id().id());
  }
  return true;
}

CloneRemap BuildIdentityRemap(
    CloneRemap const& object_remap,
    std::vector<ae::ObjId::Type> const& fresh_base_ids) {
  CloneRemap identity_remap;
  for (auto const& [_, new_id_raw] : object_remap) {
    identity_remap[new_id_raw] = new_id_raw;
  }
  for (ae::ObjId::Type base_id : fresh_base_ids) {
    identity_remap[base_id] = base_id;
  }
  return identity_remap;
}

}  // namespace

CloneCanonicalResult CloneCanonicalRoot(ae::Domain& source, ae::Obj& source_root,
                                        ae::Domain& target,
                                        CloneOptions const& options) {
  if (!options.stable_origin.is_valid()) {
    return Fail(CloneError::kMissingStableOrigin);
  }

  ae::ObjId const source_id = source_root.obj_id;
  if (!source_id.is_valid()) {
    return Fail(CloneError::kUnsupportedGraph);
  }

  ae::RamDomainStorage snapshot;
  std::set<ae::ObjId> copy_set;
  if (!BuildCanonicalSnapshot(source, source_id, options, snapshot,
                              copy_set)) {
    return Fail(CloneError::kUnsupportedGraph);
  }

  CloneRemap remap;
  ae::ObjId new_root_id =
      options.new_root_id.value_or(ae::ObjId::GenerateUnique());
  if (!new_root_id.is_valid()) {
    return Fail(CloneError::kObjIdConflict);
  }
  if (RemapIdReserved(new_root_id.id(), source_id.id(), copy_set,
                      options.boundary_ids, remap, target)) {
    return Fail(CloneError::kObjIdConflict);
  }
  remap[source_id.id()] = new_root_id.id();

  for (ae::ObjId const old_id : copy_set) {
    if (old_id == source_id) {
      continue;
    }
    ae::ObjId const allocated = AllocateCloneObjId(
        source_id.id(), copy_set, options.boundary_ids, remap, target);
    if (!allocated.is_valid()) {
      return Fail(CloneError::kObjIdConflict);
    }
    remap[old_id.id()] = allocated.id();
  }

  ae::RamDomainStorage staging_overlay_storage = snapshot;
  std::set<ae::ObjId> staging_supply_constants;
  for (ae::ObjId const id : options.boundary_ids) {
    if (copy_set.count(id) == 0) {
      staging_supply_constants.insert(id);
    }
  }
  apptraverse::OverlayDomainStorage staging_storage{
      staging_overlay_storage, source.domain_storage(),
      staging_supply_constants};
  ae::Domain staging_domain{staging_storage};
  std::vector<ae::Ptr<ae::Obj>> staging_keepalive;
  staging_keepalive.reserve(copy_set.size());
  ae::DomainGraph staging_load_graph{&staging_domain};
  std::unordered_map<ae::ObjId::Type, ae::ObjId> origins;
  for (ae::ObjId const id : copy_set) {
    ae::Ptr<ae::Obj> loaded;
    if (!staging_load_graph.LoadRoot(id, loaded).IsOk() || !loaded) {
      return Fail(CloneError::kLoadFailed);
    }
    staging_keepalive.push_back(loaded);
    origins[id.id()] = loaded->origin_id();
  }

  ae::RamDomainStorage remapped_scratch;
  ae::Domain remapped_domain{remapped_scratch};
  ae::DomainGraph remapped_save_graph{
      &remapped_domain, ae::GraphSerializationScope::CanonicalClone};
  remapped_save_graph.SetCloneSaveRemap(&remap);
  for (ae::ObjId const old_id : copy_set) {
    ae::Ptr<ae::Obj> staging_obj = staging_domain.Find(old_id);
    if (!staging_obj) {
      return Fail(CloneError::kLoadFailed);
    }
    if (!remapped_save_graph.SaveRoot(staging_obj, old_id).IsOk()) {
      return Fail(CloneError::kPersistFailed);
    }
  }

  if (!InjectAllLayeredObjects(remapped_scratch, target.domain_storage())) {
    return Fail(CloneError::kPersistFailed);
  }

  ae::DomainGraph target_load_graph{&target};
  std::vector<ae::Ptr<ae::Obj>> target_keepalive;
  target_keepalive.reserve(remap.size());
  for (auto const& [_, new_id_raw] : remap) {
    ae::Ptr<ae::Obj> loaded;
    if (!target_load_graph.LoadRoot(ae::ObjId{new_id_raw}, loaded).IsOk() ||
        !loaded) {
      return Fail(CloneError::kLoadFailed);
    }
    target_keepalive.push_back(loaded);
  }

  ae::Ptr<ae::Obj> loaded_root = target.Find(new_root_id);
  if (!loaded_root || loaded_root->GetClassId() != source_root.GetClassId()) {
    return Fail(CloneError::kLoadFailed);
  }

  loaded_root->SetOriginId(options.stable_origin);

  std::vector<ae::Ptr<ae::Obj>> cloned_copies;
  cloned_copies.reserve(remap.size());
  for (auto const& [_, new_id_raw] : remap) {
    ae::Ptr<ae::Obj> copy = target.Find(ae::ObjId{new_id_raw});
    if (!copy) {
      return Fail(CloneError::kLoadFailed);
    }
    cloned_copies.push_back(copy);
  }

  for (auto const& [old_id, new_id_raw] : remap) {
    if (old_id == source_id.id()) {
      continue;
    }
    ae::ObjId const new_id{new_id_raw};
    ae::Ptr<ae::Obj> cloned_child = target.Find(new_id);
    if (!cloned_child) {
      return Fail(CloneError::kLoadFailed);
    }
    auto const origin_it = origins.find(old_id);
    if (origin_it != origins.end() && origin_it->second.is_valid()) {
      cloned_child->SetOriginId(origin_it->second);
    }
  }

  std::vector<ae::ObjId::Type> fresh_base_ids;
  if (!InitializeFreshHistoryForClonedNodes(cloned_copies, fresh_base_ids)) {
    return Fail(CloneError::kLoadFailed);
  }

  CloneRemap const identity_remap = BuildIdentityRemap(remap, fresh_base_ids);
  ae::DomainGraph persist_graph{&target};
  persist_graph.SetCloneSaveRemap(&identity_remap);
  for (auto const& [old_id, new_id_raw] : remap) {
    ae::Ptr<ae::Obj> persist_obj = target.Find(ae::ObjId{new_id_raw});
    if (!persist_obj) {
      return Fail(CloneError::kLoadFailed);
    }
    if (!persist_graph.SaveRoot(persist_obj, ae::ObjId{new_id_raw}).IsOk()) {
      return Fail(CloneError::kPersistFailed);
    }
  }

  ae::ObjPtr<ae::Obj> result_root{&target, new_root_id, ae::ObjFlags{},
                                   loaded_root};
  return CloneCanonicalResult{std::move(result_root), CloneError::kNone};
}

}  // namespace apptraverse

