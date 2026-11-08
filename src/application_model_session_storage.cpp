#include "apptraverse/application_model_session_storage.h"

#include <utility>
#include <vector>

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_serialization.h"

namespace apptraverse {
namespace {

ApplicationModelSessionStorage* active_session = nullptr;

std::vector<std::uint8_t> ReadLoadedBytes(ae::DomainLoad load) {
  std::vector<std::uint8_t> out;
  if (load.result != ae::DomainLoadResult::kLoaded || !load.reader) {
    return out;
  }
  for (;;) {
    std::uint8_t byte = 0;
    ae::seri::SeriResult const result =
        load.reader->Read(ae::seri::DataReadTag{&byte, 1});
    if (!result) {
      if (result.error().error_code == ae::seri::read_eof.error_code) {
        break;
      }
      WriteFatalStderr("fatal: session storage read layer failed\n");
      std::abort();
    }
    out.push_back(byte);
  }
  return out;
}

void FailIfWriteError(ae::seri::SeriResult const& result) {
  if (!result) {
    WriteFatalStderr("fatal: session storage flush write failed\n");
    std::abort();
  }
}

}  // namespace

ApplicationModelSessionStorage::ApplicationModelSessionStorage(
    DirectoryDomainStorage& disk_user, EmbeddedDistilledGraph const& graph)
    : disk_user_{disk_user},
      root_id_{graph.root_object_id},
      supply_constant_ids_{EmbeddedSupplyConstantIdSet(graph)},
      persisted_user_overlay_{disk_user_, embedded_backing_,
                             supply_constant_ids_},
      model_overlay_{session_overlay_, persisted_user_overlay_,
                     supply_constant_ids_} {
  InstallEmbeddedDistilledBacking(embedded_backing_, graph);
}

void ApplicationModelSessionStorage::Activate() {
  active_session = this;
}

void ApplicationModelSessionStorage::Deactivate() {
  if (active_session == this) {
    active_session = nullptr;
  }
}

ApplicationModelSessionStorage* ApplicationModelSessionStorage::Active() {
  return active_session;
}

ApplicationModelSessionStorage::ActivateScope::ActivateScope(
    ApplicationModelSessionStorage& storage)
    : previous_{active_session} {
  storage.Activate();
}

ApplicationModelSessionStorage::ActivateScope::~ActivateScope() {
  if (previous_ != nullptr) {
    previous_->Activate();
  } else {
    active_session = nullptr;
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
      disk_user_.Remove(obj_id);
      continue;
    }

    for (auto const& [class_id, versions] : *class_map_opt) {
      for (auto const& [version, data] : versions) {
        ae::DomainQuery query{obj_id, class_id, version};
        query.force_materialize = true;
        std::unique_ptr<ae::IDomainStorageWriter> const writer =
            disk_user_.Store(query);
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
    ae::Obj& subgraph_root) {
  ae::RamDomainStorage staged;
  SavePersistedObjectGraphToRam(subgraph_root, staged);
  FlushRamToDisk(staged, nullptr, false);
}

void ApplicationModelSessionStorage::FlushEntireSessionOverlayToDisk() {
  FlushSessionLayers(nullptr);
}

}  // namespace apptraverse
