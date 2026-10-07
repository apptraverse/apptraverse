#include "apptraverse/partitioned_model_domain_storage.h"

#include "aether-objects/obj/registry.h"

#include "apptraverse/model_persistence.h"
#include "apptraverse/presenter.h"

namespace apptraverse {
namespace {

bool ClassIdUsesTransientPartition(std::uint32_t class_id) {
  if (IsRuntimeOnlyClassId(class_id)) {
    return true;
  }
  return ae::Registry::GetRegistry().GenerationDistance(Presenter::kClassId,
                                                        class_id) >= 0;
}

}  // namespace

PartitionedModelDomainStorage::PartitionedModelDomainStorage(
    ae::IDomainStorage& durable_backing)
    : durable_backing_{durable_backing} {}

bool PartitionedModelDomainStorage::QueryUsesTransientPartition(
    ae::DomainQuery const& query) {
  return ClassIdUsesTransientPartition(query.class_id);
}

std::unique_ptr<ae::IDomainStorageWriter> PartitionedModelDomainStorage::Store(
    ae::DomainQuery const& query) {
  if (QueryUsesTransientPartition(query)) {
    return transient_.Store(query);
  }
  return durable_backing_.Store(query);
}

ae::ClassList PartitionedModelDomainStorage::Enumerate(
    ae::ObjId const& obj_id) {
  ae::ClassList merged;
  auto const durable = durable_backing_.Enumerate(obj_id);
  auto const ram = transient_.Enumerate(obj_id);
  merged.insert(merged.end(), durable.begin(), durable.end());
  for (auto const id : ram) {
    if (std::find(merged.begin(), merged.end(), id) == merged.end()) {
      merged.push_back(id);
    }
  }
  return merged;
}

ae::DomainLoad PartitionedModelDomainStorage::Load(
    ae::DomainQuery const& query) {
  if (QueryUsesTransientPartition(query)) {
    return transient_.Load(query);
  }
  return durable_backing_.Load(query);
}

void PartitionedModelDomainStorage::Remove(ae::ObjId const& obj_id) {
  transient_.Remove(obj_id);
  durable_backing_.Remove(obj_id);
}

void PartitionedModelDomainStorage::CleanUp() {
  transient_.CleanUp();
  durable_backing_.CleanUp();
}

}  // namespace apptraverse
