#include "apptraverse/overlay_domain_storage.h"

#include <set>

namespace apptraverse {

OverlayDomainStorage::OverlayDomainStorage(ae::IDomainStorage& backing)
    : writable_overlay_{overlay_}, backing_{backing} {}

OverlayDomainStorage::OverlayDomainStorage(ae::IDomainStorage& writable_overlay,
                                           ae::IDomainStorage& backing)
    : writable_overlay_{writable_overlay}, backing_{backing} {}

OverlayDomainStorage::~OverlayDomainStorage() = default;

std::unique_ptr<ae::IDomainStorageWriter> OverlayDomainStorage::Store(
    ae::DomainQuery const& query) {
  return writable_overlay_.Store(query);
}

ae::ClassList OverlayDomainStorage::Enumerate(ae::ObjId const& obj_id) {
  std::set<std::uint32_t> classes;
  for (auto class_id : writable_overlay_.Enumerate(obj_id)) {
    classes.insert(class_id);
  }
  for (auto class_id : backing_.Enumerate(obj_id)) {
    classes.insert(class_id);
  }
  return ae::ClassList{classes.begin(), classes.end()};
}

ae::DomainLoad OverlayDomainStorage::Load(ae::DomainQuery const& query) {
  auto overlay_load = writable_overlay_.Load(query);
  if (overlay_load.result == ae::DomainLoadResult::kLoaded ||
      overlay_load.result == ae::DomainLoadResult::kRemoved) {
    return overlay_load;
  }
  return backing_.Load(query);
}

void OverlayDomainStorage::Remove(ae::ObjId const& obj_id) {
  // Static graph runtime does not use Remove; keep overlay-only minimal mark.
  writable_overlay_.Remove(obj_id);
}

void OverlayDomainStorage::CleanUp() { writable_overlay_.CleanUp(); }

}  // namespace apptraverse
