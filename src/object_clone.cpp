#include "apptraverse/object_clone.h"


#include <algorithm>
#include <cstdio>
#include <deque>
#include <set>
#include <source_location>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/registry.h"

#include "apptraverse/event.h"
#include "apptraverse/graph_copy_policy.h"
#include "apptraverse/graph_walk.h"
#include "apptraverse/node.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/operation_storage.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse {

bool RemapLiveObjectGraph(
    ae::Domain& target, ae::Ptr<ae::Obj> const& root,
    std::set<ae::ObjId> const& boundary_ids,
    std::set<ae::ObjId> const& canonical_shared_ids,
    std::map<ae::ObjId, ae::Ptr<ae::Obj>> const& extra_live,
    std::map<ae::ObjId, ae::ObjId> const* id_remap);

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

bool ObjectIdOccupiedInStorage(ae::IDomainStorage& storage, ae::ObjId id) {
  return !storage.Enumerate(id).empty();
}

bool ObjectIdOccupiedInDomain(ae::Domain& domain, ae::ObjId id) {
  if (domain.Find(id)) { return true; }
  for (ae::Domain* current = &domain; current != nullptr;
       current = current->parent_domain()) {
    if (ObjectIdOccupiedInStorage(current->domain_storage(), id)) { return true; }
  }
  return false;
}

CloneCanonicalResult Fail(CloneError error,
                          std::source_location location = std::source_location::current()) {
  if (std::getenv("APPTRAVERSE_CLONE_FAIL_TRACE") != nullptr) {
    std::fprintf(stderr, "CloneCanonicalRoot error=%d line=%u\n",
                 static_cast<int>(error), location.line());
  }
  return CloneCanonicalResult{{}, error};
}

using CloneRemap = ae::DomainGraph::CloneSaveRemapTable;

bool RemapSingleLiveObjectInPlace(
    ae::Domain& target, ae::Ptr<ae::Obj> const& canonical,
    ae::ObjId const object_id, std::map<ae::ObjId, ae::ObjId> const& id_remap,
    std::map<ae::ObjId, ae::Ptr<ae::Obj>> const& live_registry,
    detail::OperationMode mode = detail::OperationMode::kRemap) {
  bool remap_ok = true;
  detail::OperationStorage remap_storage{
      mode,
      /*validation_storage=*/nullptr,
      /*most_derived_classes=*/nullptr,
      &target,
      /*real_storage=*/nullptr,
      &id_remap,
      &live_registry,
      &remap_ok};
  ae::Domain remap_domain{remap_storage};
  ae::DomainGraph remap_graph{&remap_domain};
  CloneRemap traverse_remap;
  if (mode != detail::OperationMode::kCloneRebind) {
    for (auto const& [old_id, new_id] : id_remap) {
      traverse_remap[old_id.id()] = new_id.id();
    }
  }
  // Clone finalization visits every copied object explicitly. Boundary IDs
  // belong in pointer mappings, but their external graphs must not be visited.
  traverse_remap[object_id.id()] = object_id.id();
  remap_graph.SetCloneSaveRemap(&traverse_remap);
  if (!remap_graph.SaveRoot(canonical, object_id).IsOk()) {
    return false;
  }
  return remap_ok;
}

bool RebindCloneGraphOnTarget(
    ae::Domain& target, std::vector<ae::Ptr<ae::Obj>> const& cloned_locals,
    std::set<ae::ObjId> const& boundary_ids) {
  std::map<ae::ObjId, ae::ObjId> identity_remap;
  std::map<ae::ObjId, ae::Ptr<ae::Obj>> live_registry;
  for (ae::Ptr<ae::Obj> const& obj : cloned_locals) {
    identity_remap[obj->obj_id] = obj->obj_id;
    live_registry[obj->obj_id] = obj;
  }
  for (ae::ObjId const boundary_id : boundary_ids) {
    // Even an unloaded boundary must be rebound to the persistent target domain.
    identity_remap[boundary_id] = boundary_id;
    if (ae::Ptr<ae::Obj> const bound = target.Find(boundary_id)) {
      live_registry[boundary_id] = bound;
    }
  }
  for (ae::Ptr<ae::Obj> const& obj : cloned_locals) {
    // Serialization remaps ObjectLink, but plain lazy ObjPtr fields otherwise
    // retain the child-domain address. Bind every persistent edge while all
    // copied objects are still held, preserving its on-disk lazy flags.
    bool edges_ok = true;
    ForEachGraphEdgeObjPtrOnObject(*obj, [&](ae::ObjectPtrBase& edge) {
      if (!edge.is_valid()) { return; }
      ae::ObjId const id = edge.id();
      ae::Ptr<ae::Obj> resolved = target.Find(id);
      if (!resolved && edge.is_loaded()) {
        edges_ok = false;
        return;
      }
      edge = ae::ObjectPtrBase{&target, id, edge.flags(), std::move(resolved)};
    });
    if (!edges_ok) { return false; }
    if (!RemapSingleLiveObjectInPlace(
            target, obj, obj->obj_id, identity_remap, live_registry,
            detail::OperationMode::kCloneRebind)) {
      return false;
    }
  }
  return true;
}

bool FinalizeCloneFromChildDomain(
    ae::Domain& child_domain, ae::Domain& target, CloneRemap const& remap,
    ae::ObjId new_root_id, std::set<ae::ObjId> const& boundary_ids,
    std::vector<ae::Ptr<ae::Obj>>& cloned_locals_out) {
  ae::DomainGraph child_load{&child_domain};
  std::vector<ae::Ptr<ae::Obj>> child_keepalive;
  child_keepalive.reserve(remap.size());

  auto load_clone_id = [&](ae::ObjId id) -> bool {
    ae::Ptr<ae::Obj> loaded;
    if (!child_load.LoadRoot(id, loaded).IsOk() || !loaded) {
      return false;
    }
    child_keepalive.push_back(loaded);
    return true;
  };

  if (!load_clone_id(new_root_id)) {
    return false;
  }
  for (auto const& [_, new_raw] : remap) {
    ae::ObjId const new_id{new_raw};
    if (new_id == new_root_id) {
      continue;
    }
    if (!load_clone_id(new_id)) {
      return false;
    }
  }

  cloned_locals_out.clear();
  cloned_locals_out.reserve(remap.size());
  for (ae::Ptr<ae::Obj> const& obj : child_keepalive) {
    if (obj->domain != &child_domain) {
      continue;
    }
    ae::ObjId const id = obj->obj_id;
    child_domain.RemoveObject(obj.get());
    target.AddObject(id, obj);
    cloned_locals_out.push_back(obj);
  }

  return RebindCloneGraphOnTarget(target, cloned_locals_out, boundary_ids);
}

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
  // Keep source-local loading isolated while preserving inherited storage for
  // unloaded copy-closure objects. Live source objects are resolved above.
  storage_reader.set_parent_domain(source.parent_domain());
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

ae::ObjId ResolveCloneOriginForCopy(
    ae::ObjId::Type old_id_raw, ae::ObjId source_id,
    std::unordered_map<ae::ObjId::Type, ae::ObjId> const& source_origin_by_id,
    CloneOptions const& options) {
  if (old_id_raw == source_id.id()) {
    return options.stable_origin;
  }
  auto const it = source_origin_by_id.find(old_id_raw);
  ae::ObjId const from_source =
      it != source_origin_by_id.end() ? it->second : ae::ObjId{};
  if (from_source.is_valid()) {
    return from_source;
  }
  ae::ObjId const old_id{old_id_raw};
  if (options.stable_prefab_ids.count(old_id) != 0) {
    return old_id;
  }
  return ae::ObjId{};
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

bool InjectAllLayeredObjects(ae::RamDomainStorage const& scratch,
                             ae::IDomainStorage& storage) {
  for (auto const& [object_id, class_map_opt] : scratch.state) {
    if (!class_map_opt || class_map_opt->empty()) {
      continue;
    }
    for (auto const& [class_id, versions] : *class_map_opt) {
      for (auto const& [version, data] : versions) {
        auto writer = storage.Store({object_id, class_id, version});
        if (!writer) {
          return false;
        }
        if (!data.empty() &&
            !writer->Write(
                ae::seri::DataWriteTag{data.data(), data.size()})) {
          return false;
        }
      }
    }
  }
  return true;
}

bool HydrateLiveObjectFromLayerScratch(
    ae::Domain& target, ae::Obj& object, ae::ObjId storage_id,
    ae::RamDomainStorage const& layered_scratch,
    std::set<ae::ObjId> const& live_reference_ids) {
  auto fail = [](char const* stage, ae::ObjId id,
                 std::uint32_t class_id = 0, int error = 0) {
    std::fprintf(stderr,
                 "Historical hydration failed at %s (object=%lu class=%lu error=%d)\n",
                 stage, static_cast<unsigned long>(id.id()),
                 static_cast<unsigned long>(class_id), error);
    return false;
  };
  ae::Ptr<ae::Obj> const root = target.Find(object.obj_id);
  if (!root || root.get() != &object || !storage_id.is_valid()) {
    return fail("root identity", object.obj_id);
  }

  // Scratch bytes are an operation input, never a persistent USER overlay.
  ae::RamDomainStorage scratch = layered_scratch;
  OverlayDomainStorage read_storage{scratch, target.domain_storage()};
  ae::DomainGraph graph{&target};
  graph.SetLoadStorage(&read_storage);
  graph.retain_loaded_references = true;
  auto& registry = ae::Registry::GetRegistry();
  std::map<ae::ObjId, std::vector<std::uint32_t>> layers;
  std::map<ae::ObjId, ae::Ptr<ae::Obj>> objects;
  objects[storage_id] = root;

  // Construct every private leaf before loading any fields. Even a lazy edge
  // can then hold its canonical owning Ptr without referring to scratch storage
  // or to a temporary domain after this operation ends.
  for (auto const& [id, class_map] : scratch.state) {
    if (!class_map || class_map->empty() ||
        (id != storage_id && live_reference_ids.count(id) != 0)) {
      continue;
    }
    auto& classes = layers[id];
    for (auto const& [class_id, _] : *class_map) {
      auto* factory = registry.FindFactory(class_id);
      if (factory == nullptr || factory->create == nullptr ||
          factory->load == nullptr) {
        return fail("class factory", id, class_id);
      }
      classes.push_back(class_id);
    }
    std::sort(classes.begin(), classes.end(), [&](auto left, auto right) {
      return registry.GenerationDistance(left, right) > 0;
    });
    for (auto const class_id : classes) {
      if (registry.GenerationDistance(class_id, classes.back()) < 0) {
        return fail("class inheritance", id, class_id);
      }
    }
    if (id == storage_id) {
      continue;
    }
    // A private scratch ID must not overwrite an existing live or historical
    // object. The capture/replay remap allocates fresh IDs for these leaves.
    if (target.Find(id)) {
      return fail("private ID conflict", id);
    }
    auto created = registry.FindFactory(classes.back())->create();
    if (!created) {
      return fail("object construction", id, classes.back());
    }
    target.AddObject(id, created);
    objects[id] = std::move(created);
  }
  if (layers.find(storage_id) == layers.end()) {
    return fail("missing root layers", storage_id);
  }
  for (auto const& [id, classes] : layers) {
    auto& instance = objects.at(id);
    for (auto const class_id : classes) {
      auto const result = registry.FindFactory(class_id)->load(&graph, instance,
                                                              id);
      if (result.has_value() && !result->IsOk()) {
        return fail("layer load", id, class_id, result->error().error_code);
      }
    }
  }
  return true;
}

std::map<ae::ObjId, ae::Ptr<ae::Obj>> BuildLiveRegistryForRemap(
    ae::Domain& target, std::set<ae::ObjId> const& boundary_ids,
    std::set<ae::ObjId> const& canonical_shared_ids,
    std::map<ae::ObjId, ae::Ptr<ae::Obj>> const& extra_live) {
  std::map<ae::ObjId, ae::Ptr<ae::Obj>> live = extra_live;
  for (ae::ObjId const boundary_id : boundary_ids) {
    if (ae::Ptr<ae::Obj> const bound = target.Find(boundary_id)) {
      live[boundary_id] = bound;
    }
  }
  for (ae::ObjId const shared_id : canonical_shared_ids) {
    if (!shared_id.is_valid()) {
      continue;
    }
    if (ae::Ptr<ae::Obj> const bound = target.Find(shared_id)) {
      live[shared_id] = bound;
    }
  }
  return live;
}

bool RemapLiveObjectGraph(
    ae::Domain& target, ae::Ptr<ae::Obj> const& root,
    std::set<ae::ObjId> const& boundary_ids,
    std::set<ae::ObjId> const& canonical_shared_ids,
    std::map<ae::ObjId, ae::Ptr<ae::Obj>> const& extra_live,
    std::map<ae::ObjId, ae::ObjId> const* id_remap) {
  std::map<ae::ObjId, ae::ObjId> remap_table;
  if (id_remap != nullptr) {
    remap_table = *id_remap;
  }
  std::map<ae::ObjId, ae::Ptr<ae::Obj>> live =
      BuildLiveRegistryForRemap(target, boundary_ids, canonical_shared_ids,
                                extra_live);
  live[root->obj_id] = root;
  if (id_remap != nullptr) {
    for (auto const& [scratch_id, live_id] : *id_remap) {
      if (scratch_id == live_id) {
        continue;
      }
      auto const live_it = live.find(live_id);
      if (live_it != live.end() && live_it->second) {
        live[scratch_id] = live_it->second;
      } else if (ae::Ptr<ae::Obj> const bound = target.Find(live_id)) {
        live[live_id] = bound;
        live[scratch_id] = bound;
      }
    }
  }
  for (auto const& [obj_id, _] : live) {
    if (remap_table.find(obj_id) == remap_table.end()) {
      remap_table[obj_id] = obj_id;
    }
  }
  if (!RemapSingleLiveObjectInPlace(target, root, root->obj_id, remap_table,
                                    live)) {
    return false;
  }
  return true;
}

namespace detail {

bool InjectLayeredObjects(ae::RamDomainStorage const& scratch,
                            ae::IDomainStorage& storage) {
  for (auto const& [object_id, class_map_opt] : scratch.state) {
    if (!class_map_opt || class_map_opt->empty()) {
      continue;
    }
    for (auto const& [class_id, versions] : *class_map_opt) {
      for (auto const& [version, data] : versions) {
        auto writer = storage.Store(
            ae::DomainQuery{object_id, class_id, version});
        if (writer == nullptr) {
          return false;
        }
        if (!data.empty() &&
            !writer->Write(ae::seri::DataWriteTag{data.data(), data.size()})) {
          return false;
        }
      }
    }
  }
  return true;
}

ae::ObjId AllocateCloneObjIdForHistorical(
    ae::ObjId::Type source_root, std::set<ae::ObjId> const& copy_set,
    CloneRemap const& remap, ae::Domain& target) {
  std::set<ae::ObjId> const empty_boundary;
  for (int attempt = 0; attempt < 64; ++attempt) {
    ae::ObjId const candidate = ae::ObjId::GenerateUnique();
    if (candidate.id() == 0u || candidate.id() == source_root) {
      continue;
    }
    bool reserved = false;
    for (ae::ObjId const id : copy_set) {
      if (id.id() == candidate.id()) {
        reserved = true;
        break;
      }
    }
    for (auto const& [_, mapped] : remap) {
      if (mapped == candidate.id()) {
        reserved = true;
        break;
      }
    }
    if (!reserved) {
      if (!ObjectIdOccupiedInDomain(target, candidate)) {
        return candidate;
      }
    }
    (void)empty_boundary;
  }
  return ae::ObjId{};
}

}  // namespace detail

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
  staging_domain.set_parent_domain(&source);
  std::vector<ae::Ptr<ae::Obj>> staging_keepalive;
  staging_keepalive.reserve(copy_set.size());
  ae::DomainGraph staging_load_graph{&staging_domain};
  staging_load_graph.retain_loaded_references = true;
  std::unordered_map<ae::ObjId::Type, ae::ObjId> origins;
  // Declare the entire copy set locally before loading fields. Otherwise an
  // ancestor lookup can resolve an internal edge to the source object, or an
  // external live-only object requires an unnecessary storage round-trip.
  for (ae::ObjId const id : copy_set) {
    ae::Ptr<ae::Obj> source_object = source.Find(id);
    if (!source_object) {
      ae::DomainGraph source_load{&source};
      if (!source_load.LoadRoot(id, source_object).IsOk() || !source_object) {
        return Fail(CloneError::kLoadFailed);
      }
    }
    auto* factory = ae::Registry::GetRegistry().FindFactory(source_object->GetClassId());
    if (factory == nullptr || factory->create == nullptr) {
      return Fail(CloneError::kLoadFailed);
    }
    auto created = factory->create();
    if (!created) { return Fail(CloneError::kLoadFailed); }
    staging_domain.AddObject(id, created);
    staging_keepalive.push_back(std::move(created));
  }
  for (auto& loaded : staging_keepalive) {
    auto& registry = ae::Registry::GetRegistry();
    auto classes = staging_storage.Enumerate(loaded->obj_id);
    std::sort(classes.begin(), classes.end(), [&](auto left, auto right) {
      return registry.GenerationDistance(left, right) > 0;
    });
    for (auto class_id : classes) {
      auto* factory = registry.FindFactory(class_id);
      if (factory == nullptr || factory->load == nullptr) {
        return Fail(CloneError::kLoadFailed);
      }
      auto const result = factory->load(&staging_load_graph, loaded, loaded->obj_id);
      if (result.has_value() && !result->IsOk()) {
        return Fail(CloneError::kLoadFailed);
      }
    }
    origins[loaded->obj_id.id()] = loaded->origin_id();
  }

  std::unordered_map<ae::ObjId::Type, ae::ObjId> resolved_origins;
  resolved_origins.reserve(copy_set.size());
  for (ae::ObjId const id : copy_set) {
    ae::ObjId const resolved = ResolveCloneOriginForCopy(
        id.id(), source_id, origins, options);
    if (!resolved.is_valid()) {
      return Fail(CloneError::kMissingStableOrigin);
    }
    resolved_origins[id.id()] = resolved;
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

  std::vector<ae::Ptr<ae::Obj>> cloned_copies;
  if (options.persist_layers_to_target_storage) {
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
    cloned_copies = std::move(target_keepalive);
  } else {
    std::set<ae::ObjId> child_constants = options.boundary_ids;
    child_constants.insert(options.stable_prefab_ids.begin(),
                           options.stable_prefab_ids.end());
    if (options.stable_origin.is_valid()) {
      child_constants.insert(options.stable_origin);
    }
    OverlayDomainStorage child_storage{remapped_scratch,
                                       target.domain_storage(), child_constants};
    ae::Domain child_domain{child_storage};
    child_domain.set_parent_domain(&target);
    if (!FinalizeCloneFromChildDomain(child_domain, target, remap, new_root_id,
                                      options.boundary_ids, cloned_copies)) {
      return Fail(CloneError::kLoadFailed);
    }
  }

  ae::Ptr<ae::Obj> loaded_root = target.Find(new_root_id);
  if (!loaded_root || loaded_root->GetClassId() != source_root.GetClassId()) {
    return Fail(CloneError::kLoadFailed);
  }

  for (auto const& [old_id, new_id_raw] : remap) {
    ae::ObjId const new_id{new_id_raw};
    ae::Ptr<ae::Obj> cloned_copy = target.Find(new_id);
    if (!cloned_copy) {
      return Fail(CloneError::kLoadFailed);
    }
    auto const origin_it = resolved_origins.find(old_id);
    if (origin_it == resolved_origins.end() ||
        !origin_it->second.is_valid()) {
      return Fail(CloneError::kMissingStableOrigin);
    }
    cloned_copy->SetOriginId(origin_it->second);
  }

  std::vector<ae::ObjId::Type> fresh_base_ids;
  if (!InitializeFreshHistoryForClonedNodes(cloned_copies, fresh_base_ids)) {
    return Fail(CloneError::kLoadFailed);
  }

  if (options.persist_layers_to_target_storage) {
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
  }

  ae::ObjPtr<ae::Obj> result_root{&target, new_root_id, ae::ObjFlags{},
                                   loaded_root};
  return CloneCanonicalResult{std::move(result_root), CloneError::kNone};
}

}  // namespace apptraverse

