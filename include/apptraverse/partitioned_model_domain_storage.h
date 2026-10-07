#ifndef APPTRAVERSE_PARTITIONED_MODEL_DOMAIN_STORAGE_H_
#define APPTRAVERSE_PARTITIONED_MODEL_DOMAIN_STORAGE_H_

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/idomain_storage.h"

namespace apptraverse {

// Live model Domain storage: durable product state uses backing; runtime-only
// class layers (RegisterRuntimeOnlyClassId) and presenters use in-memory RAM
// only. Load never reads runtime-only layers from backing, so legacy runtime
// blobs on disk are ignored after upgrade.
class PartitionedModelDomainStorage final : public ae::IDomainStorage {
 public:
  explicit PartitionedModelDomainStorage(ae::IDomainStorage& durable_backing);

  std::unique_ptr<ae::IDomainStorageWriter> Store(
      ae::DomainQuery const& query) override;
  ae::ClassList Enumerate(ae::ObjId const& obj_id) override;
  ae::DomainLoad Load(ae::DomainQuery const& query) override;
  void Remove(ae::ObjId const& obj_id) override;
  void CleanUp() override;

  ae::IDomainStorage& durable_backing() { return durable_backing_; }
  ae::IDomainStorage const& durable_backing() const { return durable_backing_; }
  ae::RamDomainStorage& transient() { return transient_; }
  ae::RamDomainStorage const& transient() const { return transient_; }

  static bool QueryUsesTransientPartition(ae::DomainQuery const& query);

 private:
  ae::IDomainStorage& durable_backing_;
  ae::RamDomainStorage transient_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_PARTITIONED_MODEL_DOMAIN_STORAGE_H_
