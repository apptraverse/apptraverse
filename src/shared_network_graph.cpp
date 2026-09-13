#include "apptraverse/shared_network_graph.h"

#include <cassert>
#include <memory>
#include <utility>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj.h"

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
                       ae::IDomainStorage& dst) {
  auto const it = src.state.find(obj_id);
  if (it == src.state.end() || !it->second.has_value()) {
    return;
  }
  for (auto const& [class_id, versions] : *it->second) {
    for (auto const& [version, data] : versions) {
      auto writer = dst.Store(ae::DomainQuery{obj_id, class_id, version});
      assert(writer != nullptr);
      if (!data.empty()) {
        auto const result = writer->Write(
            ae::seri::DataWriteTag{data.data(), data.size()});
        assert(result);
        (void)result;
      }
    }
  }
}

}  // namespace

void CopySharedNetworkGraph(SharedNode::ptr source,
                            ae::IDomainStorage& source_storage,
                            ae::Domain& target_domain,
                            ae::IDomainStorage& target_storage) {
  assert(source.is_valid());
  assert(source.is_loaded());
  (void)target_domain;

  source.Save();

  ae::RamDomainStorage scratch;
  ReadThroughDomainStorage read_through{source_storage, scratch};
  ae::Domain scratch_domain{read_through};
  auto scratch_root = SharedNode::ptr::Declare(
      ae::CreateWith{scratch_domain}.with_id(source.id()));
  scratch_root.Load();
  assert(scratch_root.is_loaded());

  // Sanitize on the scratch copy only — live source LocalPtr edges stay.
  scratch_root->ClearLocalPersistentEdges();
  scratch_root.Save();

  for (auto const& [obj_id, classes] : scratch.state) {
    if (!classes.has_value()) {
      continue;
    }
    TransferRamObject(scratch, obj_id, target_storage);
  }
}

}  // namespace apptraverse
