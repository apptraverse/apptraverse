#include "apptraverse/partitioned_model_domain_storage.h"

#include "apptraverse/directory_domain_storage.h"

namespace apptraverse {

PartitionedModelDomainStorage::PartitionedModelDomainStorage(
    ae::IDomainStorage& durable_backing)
    : durable_backing_{durable_backing} {}

PartitionedModelDomainStorage::PartitionedModelDomainStorage(
    DirectoryDomainStorage& durable_directory)
    : durable_backing_{durable_directory} {}

std::unique_ptr<ae::IDomainStorageWriter> PartitionedModelDomainStorage::Store(
    ae::DomainQuery const& query) {
  return durable_backing_.Store(query);
}

ae::ClassList PartitionedModelDomainStorage::Enumerate(
    ae::ObjId const& obj_id) {
  return durable_backing_.Enumerate(obj_id);
}

ae::DomainLoad PartitionedModelDomainStorage::Load(
    ae::DomainQuery const& query) {
  return durable_backing_.Load(query);
}

void PartitionedModelDomainStorage::Remove(ae::ObjId const& obj_id) {
  durable_backing_.Remove(obj_id);
}

void PartitionedModelDomainStorage::CleanUp() {
  durable_backing_.CleanUp();
}

}  // namespace apptraverse
