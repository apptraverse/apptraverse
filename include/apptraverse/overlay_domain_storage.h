#ifndef APPTRAVERSE_OVERLAY_DOMAIN_STORAGE_H_
#define APPTRAVERSE_OVERLAY_DOMAIN_STORAGE_H_

#include <set>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/idomain_storage.h"

namespace apptraverse {

// Writable USER overlay over read-only BACKING distribution storage.
// Load prefers USER for dynamic objects, then BACKING. BACKING is never
// mutated through this facade.
//
// Store on dynamic layers: write USER when a USER layer already exists or
// bytes differ from BACKING; skip USER materialization when bytes match BACKING
// and no USER layer exists yet. An existing USER override is kept even when
// new bytes match BACKING again.
//
// supply_constant_ids: ObjIds whose layers always load/enumerate from BACKING
// only and are never persisted to USER. Role is ObjId membership only.
class OverlayDomainStorage final : public ae::IDomainStorage {
 public:
  explicit OverlayDomainStorage(ae::IDomainStorage& backing);
  OverlayDomainStorage(ae::IDomainStorage& writable_user,
                       ae::IDomainStorage& backing);
  OverlayDomainStorage(ae::IDomainStorage& backing,
                       std::set<ae::ObjId> supply_constant_ids);
  OverlayDomainStorage(ae::IDomainStorage& writable_user,
                       ae::IDomainStorage& backing,
                       std::set<ae::ObjId> supply_constant_ids);
  ~OverlayDomainStorage() override;

  std::unique_ptr<ae::IDomainStorageWriter> Store(
      ae::DomainQuery const& query) override;
  ae::ClassList Enumerate(ae::ObjId const& obj_id) override;
  ae::DomainLoad Load(ae::DomainQuery const& query) override;
  void Remove(ae::ObjId const& obj_id) override;
  void CleanUp() override;

  ae::RamDomainStorage& overlay() { return overlay_; }
  ae::RamDomainStorage const& overlay() const { return overlay_; }
  ae::IDomainStorage& backing() { return backing_; }
  ae::IDomainStorage const& backing() const { return backing_; }

  bool IsSupplyConstant(ae::ObjId id) const;

 private:
  ae::RamDomainStorage overlay_;
  ae::IDomainStorage& writable_overlay_;
  ae::IDomainStorage& backing_;
  std::set<ae::ObjId> supply_constant_ids_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_OVERLAY_DOMAIN_STORAGE_H_
