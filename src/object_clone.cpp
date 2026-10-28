#include "apptraverse/object_clone.h"

#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/object_serialization.h"

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

std::vector<ae::ObjId> CollectLayeredObjectIds(
    ae::RamDomainStorage const& scratch) {
  std::vector<ae::ObjId> ids;
  for (auto const& [object_id, class_map_opt] : scratch.state) {
    if (!class_map_opt || class_map_opt->empty()) {
      continue;
    }
    ids.push_back(object_id);
  }
  return ids;
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
                     std::set<ae::ObjId> const& boundary_ids,
                     std::vector<ae::ObjId> const& snapshot_ids,
                     CloneRemap const& remap, ae::Domain& target) {
  if (candidate == 0u) {
    return true;
  }
  if (candidate == source_root) {
    return true;
  }
  for (ae::ObjId const id : snapshot_ids) {
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
                             std::set<ae::ObjId> const& boundary_ids,
                             std::vector<ae::ObjId> const& snapshot_ids,
                             CloneRemap const& remap, ae::Domain& target) {
  for (int attempt = 0; attempt < 64; ++attempt) {
    ae::ObjId const candidate = ae::ObjId::GenerateUnique();
    if (!RemapIdReserved(candidate.id(), source_root, boundary_ids, snapshot_ids,
                         remap, target)) {
      return candidate;
    }
  }
  return ae::ObjId{};
}

std::set<ae::ObjId> BuildCopySet(ae::RamDomainStorage const& snapshot,
                                 ae::ObjId source_root,
                                 std::set<ae::ObjId> const& boundary_ids) {
  std::set<ae::ObjId> copy_set;
  for (ae::ObjId const id : CollectLayeredObjectIds(snapshot)) {
    if (boundary_ids.count(id) != 0) {
      continue;
    }
    copy_set.insert(id);
  }
  copy_set.insert(source_root);
  return copy_set;
}

}  // namespace

CloneCanonicalResult CloneCanonicalRoot(ae::Domain& source, ae::Obj& source_root,
                                        ae::Domain& target,
                                        CloneOptions const& options) {
  (void)source;
  if (!options.stable_origin.is_valid()) {
    return Fail(CloneError::kMissingStableOrigin);
  }

  ae::ObjId const source_id = source_root.obj_id;
  if (!source_id.is_valid()) {
    return Fail(CloneError::kUnsupportedGraph);
  }

  ae::RamDomainStorage snapshot;
  SavePersistedObjectGraphToRam(source_root, snapshot);

  std::vector<ae::ObjId> const layered = CollectLayeredObjectIds(snapshot);
  if (layered.empty()) {
    return Fail(CloneError::kUnsupportedGraph);
  }

  std::set<ae::ObjId> const copy_set =
      BuildCopySet(snapshot, source_id, options.boundary_ids);
  if (copy_set.find(source_id) == copy_set.end()) {
    return Fail(CloneError::kUnsupportedGraph);
  }

  CloneRemap remap;
  std::vector<ae::ObjId> const snapshot_ids = layered;

  ae::ObjId new_root_id =
      options.new_root_id.value_or(ae::ObjId::GenerateUnique());
  if (!new_root_id.is_valid()) {
    return Fail(CloneError::kObjIdConflict);
  }
  if (RemapIdReserved(new_root_id.id(), source_id.id(), options.boundary_ids,
                      snapshot_ids, remap, target)) {
    return Fail(CloneError::kObjIdConflict);
  }
  remap[source_id.id()] = new_root_id.id();

  for (ae::ObjId const old_id : copy_set) {
    if (old_id == source_id) {
      continue;
    }
    ae::ObjId const allocated = AllocateCloneObjId(
        source_id.id(), options.boundary_ids, snapshot_ids, remap, target);
    if (!allocated.is_valid()) {
      return Fail(CloneError::kObjIdConflict);
    }
    remap[old_id.id()] = allocated.id();
  }

  ae::RamDomainStorage staging_storage = snapshot;
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

  ae::Ptr<ae::Obj> const staging_root = staging_domain.Find(source_id);
  if (!staging_root) {
    return Fail(CloneError::kLoadFailed);
  }

  ae::RamDomainStorage remapped_scratch;
  ae::Domain remapped_domain{remapped_scratch};
  ae::DomainGraph remapped_save_graph{&remapped_domain};
  remapped_save_graph.SetCloneSaveRemap(&remap);
  if (!remapped_save_graph.SaveRoot(staging_root, source_id).IsOk()) {
    return Fail(CloneError::kPersistFailed);
  }

  if (!InjectAllLayeredObjects(remapped_scratch, target.domain_storage())) {
    return Fail(CloneError::kPersistFailed);
  }

  ae::DomainGraph target_load_graph{&target};
  ae::Ptr<ae::Obj> loaded_root;
  if (!target_load_graph.LoadRoot(new_root_id, loaded_root).IsOk() ||
      !loaded_root) {
    return Fail(CloneError::kLoadFailed);
  }
  if (loaded_root->GetClassId() != source_root.GetClassId()) {
    return Fail(CloneError::kLoadFailed);
  }

  loaded_root->SetOriginId(options.stable_origin);
  for (auto const& [old_id, new_id_raw] : remap) {
    if (old_id == source_id.id()) {
      continue;
    }
    ae::ObjId const new_id{new_id_raw};
    ae::Ptr<ae::Obj> cloned_child = target.Find(new_id);
    if (!cloned_child) {
      continue;
    }
    auto const origin_it = origins.find(old_id);
    if (origin_it != origins.end() && origin_it->second.is_valid()) {
      cloned_child->SetOriginId(origin_it->second);
    }
  }

  CloneRemap identity_remap;
  for (auto const& [_, new_id_raw] : remap) {
    identity_remap[new_id_raw] = new_id_raw;
  }
  ae::DomainGraph persist_graph{&target};
  persist_graph.SetCloneSaveRemap(&identity_remap);
  if (!persist_graph.SaveRoot(loaded_root, new_root_id).IsOk()) {
    return Fail(CloneError::kPersistFailed);
  }

  ae::ObjPtr<ae::Obj> result_root{&target, new_root_id, ae::ObjFlags{},
                                   loaded_root};
  return CloneCanonicalResult{std::move(result_root), CloneError::kNone};
}

}  // namespace apptraverse
