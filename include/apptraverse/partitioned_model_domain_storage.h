#ifndef APPTRAVERSE_PARTITIONED_MODEL_DOMAIN_STORAGE_H_
#define APPTRAVERSE_PARTITIONED_MODEL_DOMAIN_STORAGE_H_

#include "aether-objects/obj/idomain_storage.h"

namespace apptraverse {

class DirectoryDomainStorage;

// Durable model storage forwarder. All object layers use the durable backing;
// there is no class-based RAM partition.
class PartitionedModelDomainStorage final : public ae::IDomainStorage {
 public:
  explicit PartitionedModelDomainStorage(ae::IDomainStorage& durable_backing);
  explicit PartitionedModelDomainStorage(DirectoryDomainStorage& durable_directory);

  std::unique_ptr<ae::IDomainStorageWriter> Store(
      ae::DomainQuery const& query) override;
  ae::ClassList Enumerate(ae::ObjId const& obj_id) override;
  ae::DomainLoad Load(ae::DomainQuery const& query) override;
  void Remove(ae::ObjId const& obj_id) override;
  void CleanUp() override;

  ae::IDomainStorage& durable_backing() { return durable_backing_; }
  ae::IDomainStorage const& durable_backing() const { return durable_backing_; }

 private:
  ae::IDomainStorage& durable_backing_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_PARTITIONED_MODEL_DOMAIN_STORAGE_H_
