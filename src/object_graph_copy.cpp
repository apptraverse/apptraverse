#include "apptraverse/object_graph_copy.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <utility>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"

#include "apptraverse/event.h"
#include "apptraverse/node.h"
#include "apptraverse/object_graph_copy_detail.h"

namespace apptraverse {
namespace {

class ReadThroughDomainStorage final : public ae::IDomainStorage {
 public:
  ReadThroughDomainStorage(ae::IDomainStorage& source,
                           ae::RamDomainStorage& scratch)
      : source_{source}, scratch_{scratch} {}

  std::unique_ptr<ae::IDomainStorageWriter> Store(
      ae::DomainQuery const& query) override {
    return scratch_.Store(query);
  }

  ae::ClassList Enumerate(ae::ObjId const& obj_id) override {
    auto const scratch_it = scratch_.state.find(obj_id);
    if (scratch_it != scratch_.state.end()) {
      if (!scratch_it->second.has_value()) {
        return {};
      }
      return scratch_.Enumerate(obj_id);
    }
    return source_.Enumerate(obj_id);
  }

  ae::DomainLoad Load(ae::DomainQuery const& query) override {
    auto const scratch_it = scratch_.state.find(query.id);
    if (scratch_it != scratch_.state.end()) {
      auto loaded = scratch_.Load(query);
      if (loaded.result != ae::DomainLoadResult::kEmpty) {
        return loaded;
      }
      if (scratch_it->second.has_value()) {
        return {ae::DomainLoadResult::kEmpty, {}};
      }
      return {ae::DomainLoadResult::kRemoved, {}};
    }
    return source_.Load(query);
  }

  void Remove(ae::ObjId const& obj_id) override { scratch_.Remove(obj_id); }
  void CleanUp() override { scratch_.CleanUp(); }

 private:
  ae::IDomainStorage& source_;
  ae::RamDomainStorage& scratch_;
};

void TransferRamObject(ae::RamDomainStorage const& src, ae::ObjId obj_id,
                       ae::IDomainStorage& dst, bool skip_existing) {
  auto const it = src.state.find(obj_id);
  if (it == src.state.end() || !it->second.has_value()) {
    return;
  }
  for (auto const& [class_id, versions] : *it->second) {
    if (skip_existing && StorageHasClass(dst, obj_id, class_id)) {
      continue;
    }
    for (auto const& [version, data] : versions) {
      auto writer = dst.Store(ae::DomainQuery{obj_id, class_id, version});
      assert(writer != nullptr);
      if (!data.empty()) {
        writer->Write(ae::seri::DataTag{static_cast<void const*>(data.data()),
                                        data.size()});
      }
    }
  }
}

void PrepareLoadedRoot(ae::Obj& root, detail::PrepareSyncGraphContext& ctx) {
  if (auto* node = dynamic_cast<Node*>(&root)) {
    node->PrepareSyncGraph(ctx);
    return;
  }
  if (auto* event = dynamic_cast<Event*>(&root)) {
    event->PrepareSyncGraph(ctx);
  }
}

}  // namespace

bool StorageHasObject(ae::IDomainStorage& storage, ae::ObjId id) {
  return !storage.Enumerate(id).empty();
}

bool StorageHasClass(ae::IDomainStorage& storage, ae::ObjId id,
                     std::uint32_t class_id) {
  auto const classes = storage.Enumerate(id);
  return std::find(classes.begin(), classes.end(), class_id) != classes.end();
}

void CopyObjectGraph(ae::Obj::ptr source, ae::IDomainStorage& source_storage,
                     ae::Domain& target_domain,
                     ae::IDomainStorage& target_storage, SharedCopyMode mode) {
  assert(source.is_valid());
  assert(source.is_loaded());
  (void)target_domain;

  source.Save();

  ae::RamDomainStorage scratch;
  ReadThroughDomainStorage read_through{source_storage, scratch};
  ae::Domain scratch_domain{ae::Now(), read_through};
  auto scratch_root =
      ae::Obj::ptr::Declare(ae::CreateWith{scratch_domain}.with_id(source.id()));
  scratch_root.Load();
  assert(scratch_root.is_loaded());

  detail::PrepareSyncGraphContext ctx;
  ctx.dest_for_refs =
      (mode == SharedCopyMode::kReferenceExistingTargets) ? &target_storage
                                                          : nullptr;
  ctx.mode = mode;
  PrepareLoadedRoot(*scratch_root, ctx);
  scratch_root.Save();

  bool const skip_existing =
      (mode == SharedCopyMode::kReferenceExistingTargets);
  for (auto const& [obj_id, classes] : scratch.state) {
    if (!classes.has_value()) {
      continue;
    }
    TransferRamObject(scratch, obj_id, target_storage, skip_existing);
  }
}

ae::Obj::ptr ImportObjectGraph(ae::Obj::ptr source,
                               ae::IDomainStorage& source_storage,
                               SyncReplica& target, SharedCopyMode mode) {
  assert(source.is_valid());
  assert(source.is_loaded());
  auto const root_id = source.id();
  CopyObjectGraph(std::move(source), source_storage, target.domain,
                  target.storage, mode);
  auto loaded =
      ae::Obj::ptr::Declare(ae::CreateWith{target.domain}.with_id(root_id));
  loaded.Load();
  if (!loaded.is_loaded()) {
    auto const classes = target.storage.Enumerate(root_id);
    std::fprintf(stderr, "IMPORT_LOAD_FAILED root_id=%u class_count=%zu",
                 static_cast<unsigned>(root_id.id()), classes.size());
    for (auto const class_id : classes) {
      std::fprintf(stderr, " class=%u", static_cast<unsigned>(class_id));
    }
    std::fprintf(stderr, "\n");
  }
  assert(loaded.is_loaded());
  return loaded;
}

}  // namespace apptraverse
