#include "apptraverse/object_clone.h"

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

CloneCanonicalResult Fail(CloneError error) {
  return CloneCanonicalResult{{}, error};
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

  ae::ObjId new_id =
      options.new_root_id.value_or(ae::ObjId::GenerateUnique());
  if (!new_id.is_valid()) {
    return Fail(CloneError::kObjIdConflict);
  }
  if (target.Find(new_id)) {
    return Fail(CloneError::kObjIdConflict);
  }
  if (ObjectIdOccupiedInStorage(target.domain_storage(), new_id)) {
    return Fail(CloneError::kObjIdConflict);
  }

  ae::RamDomainStorage scratch;
  SavePersistedObjectGraphToRam(source_root, scratch);

  std::vector<ae::ObjId> const layered = CollectLayeredObjectIds(scratch);
  if (layered.size() != 1u || layered.front() != source_id) {
    return Fail(CloneError::kUnsupportedGraph);
  }

  auto const source_layers_it = scratch.state.find(source_id);
  if (source_layers_it == scratch.state.end() ||
      !source_layers_it->second.has_value()) {
    return Fail(CloneError::kUnsupportedGraph);
  }

  if (!InjectClassData(*source_layers_it->second, new_id,
                       target.domain_storage())) {
    return Fail(CloneError::kPersistFailed);
  }

  ae::DomainGraph load_graph{&target};
  ae::Ptr<ae::Obj> loaded;
  if (!load_graph.LoadRoot(new_id, loaded).IsOk() || !loaded) {
    return Fail(CloneError::kLoadFailed);
  }
  if (loaded->GetClassId() != source_root.GetClassId()) {
    return Fail(CloneError::kLoadFailed);
  }

  loaded->SetOriginId(options.stable_origin);

  ae::DomainGraph save_graph{&target};
  if (!save_graph.SaveRoot(loaded, new_id).IsOk()) {
    return Fail(CloneError::kPersistFailed);
  }

  return CloneCanonicalResult{new_id, CloneError::kNone};
}

}  // namespace apptraverse
