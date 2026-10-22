#ifndef APPTRAVERSE_OVERLAY_DOMAIN_STORAGE_H_
#define APPTRAVERSE_OVERLAY_DOMAIN_STORAGE_H_

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/idomain_storage.h"

namespace apptraverse {

// Writable overlay over read-only backing distribution storage.
// Load prefers overlay, then backing. Backing is never mutated here.
// Store writes the overlay when the user layer is missing only if serialized
// bytes differ from backing; unchanged layers are not materialized in USER.
// Existing USER layers are always written through the writable storage.
class OverlayDomainStorage final : public ae::IDomainStorage {
 public:
  explicit OverlayDomainStorage(ae::IDomainStorage& backing);
  OverlayDomainStorage(ae::IDomainStorage& writable_overlay,
                       ae::IDomainStorage& backing);
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

 private:
  ae::RamDomainStorage overlay_;
  ae::IDomainStorage& writable_overlay_;
  ae::IDomainStorage& backing_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_OVERLAY_DOMAIN_STORAGE_H_
