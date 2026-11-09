#include "apptraverse/application_model_session_storage.h"

#include <utility>
#include <vector>

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_serialization.h"

namespace apptraverse {
namespace {

void FailIfWriteError(ae::seri::SeriResult const& result) {
  if (!result) {
    WriteFatalStderr("fatal: session storage flush write failed\n");
    std::abort();
  }
}

void MergeRamPreferExisting(ae::RamDomainStorage& into,
                            ae::RamDomainStorage const& from) {
  for (auto const& [obj_id, from_map_opt] : from.state) {
    if (!from_map_opt) {
      into.state.erase(obj_id);
      continue;
    }
    auto& into_slot = into.state[obj_id];
    if (!into_slot) {
      into_slot = from_map_opt;
      continue;
    }
    for (auto const& [class_id, from_versions] : *from_map_opt) {
      auto& into_versions = (*into_slot)[class_id];
      for (auto const& [version, data] : from_versions) {
        if (into_versions.find(version) == into_versions.end()) {
          into_versions.emplace(version, data);
        }
      }
    }
  }
}

void OverlaySessionLayersOntoStaged(ae::RamDomainStorage& staged,
                                    ae::RamDomainStorage const& session,
                                    std::set<ae::ObjId> const& ids) {
  for (ae::ObjId const& obj_id : ids) {
    auto const session_it = session.state.find(obj_id);
    if (session_it == session.state.end() || !session_it->second) {
      continue;
    }
    auto& staged_slot = staged.state[obj_id];
    if (!staged_slot) {
      staged_slot = session_it->second;
      continue;
    }
    for (auto const& [class_id, session_versions] : *session_it->second) {
      auto& staged_versions = (*staged_slot)[class_id];
      for (auto const& [version, data] : session_versions) {
        staged_versions[version] = data;
      }
    }
  }
}

}  // namespace

ApplicationModelSessionStorage::ApplicationModelSessionStorage(
    DirectoryDomainStorage& disk_user, EmbeddedDistilledGraph const& graph,
    ae::IDomainStorage* disk_write_target)
    : disk_user_{disk_user},
      disk_write_target_{disk_write_target != nullptr ? *disk_write_target
                                                     : disk_user},
      root_id_{graph.root_object_id},
      supply_constant_ids_{EmbeddedSupplyConstantIdSet(graph)},
      persisted_user_overlay_{disk_user_, embedded_backing_,
                             supply_constant_ids_},
      model_overlay_{session_overlay_, persisted_user_overlay_,
                     supply_constant_ids_} {
  InstallEmbeddedDistilledBacking(embedded_backing_, graph);
}

ApplicationModelSessionStorage::ActivateScope::ActivateScope(
    ApplicationModelSessionStorage& storage, ApplicationModelRuntime& runtime)
    : storage_{&storage},
      runtime_{&runtime},
      previous_storage_{runtime.user_session_storage()} {
  runtime.BindUserSessionStorage(&storage);
}

ApplicationModelSessionStorage::ActivateScope::~ActivateScope() {
  if (runtime_ != nullptr) {
    runtime_->BindUserSessionStorage(previous_storage_);
  }
}

void ApplicationModelSessionStorage::FlushRamToDisk(
    ae::RamDomainStorage const& source, std::set<ae::ObjId> const* only_ids,
    bool clear_matching_session_entries) {
  if (source.state.empty()) {
    return;
  }

  std::vector<ae::ObjId> flushed_ids;
  flushed_ids.reserve(source.state.size());

  for (auto const& [obj_id, class_map_opt] : source.state) {
    if (supply_constant_ids_.count(obj_id) != 0) {
      continue;
    }
    if (only_ids != nullptr && only_ids->count(obj_id) == 0) {
      continue;
    }
    flushed_ids.push_back(obj_id);

    if (!class_map_opt) {
      disk_write_target_.Remove(obj_id);
      continue;
    }

    for (auto const& [class_id, versions] : *class_map_opt) {
      for (auto const& [version, data] : versions) {
        ae::DomainQuery query{obj_id, class_id, version};
        query.force_materialize = true;
        std::unique_ptr<ae::IDomainStorageWriter> const writer =
            disk_write_target_.Store(query);
        if (!writer) {
          WriteFatalStderr("fatal: session storage flush Store failed\n");
          std::abort();
        }
        FailIfWriteError(writer->Write(
            ae::seri::DataWriteTag{data.data(), data.size()}));
      }
    }
  }

  if (disk_user_.SaveSessionHadFailure()) {
    WriteFatalStderr("fatal: session USER flush had I/O failure\n");
    std::abort();
  }

  if (clear_matching_session_entries) {
    for (ae::ObjId const id : flushed_ids) {
      session_overlay_.state.erase(id);
    }
  }
}

void ApplicationModelSessionStorage::FlushSessionLayers(
    std::set<ae::ObjId> const* only_ids) {
  FlushRamToDisk(session_overlay_, only_ids, true);
}

void ApplicationModelSessionStorage::StagePersistentGraphInSessionOverlay(
    ae::Obj& root) {
  SavePersistedObjectGraphToRam(root, session_overlay_);
}

void ApplicationModelSessionStorage::FlushReachableSubgraphToDisk(
    ae::Obj& subgraph_root, std::set<ae::ObjId> extra_flush_ids) {
  ae::RamDomainStorage staged;
  SavePersistedObjectGraphToRam(subgraph_root, staged);

  std::set<ae::ObjId> flush_ids =
      CollectPersistedSerializationObjectIds(subgraph_root);
  for (auto const& [obj_id, class_map_opt] : staged.state) {
    if (class_map_opt) {
      flush_ids.insert(obj_id);
    }
  }
  for (ae::ObjId const id : extra_flush_ids) {
    if (id.is_valid()) {
      flush_ids.insert(id);
    }
  }
  flush_ids.insert(subgraph_root.obj_id);

  OverlaySessionLayersOntoStaged(staged, session_overlay_, flush_ids);
  FlushRamToDisk(staged, &flush_ids, false);
  for (ae::ObjId const id : flush_ids) {
    session_overlay_.state.erase(id);
  }
}

void ApplicationModelSessionStorage::FlushEntireSessionOverlayToDisk() {
  FlushSessionLayers(nullptr);
}

}  // namespace apptraverse
