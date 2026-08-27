#include "apptraverse/overlay_domain_storage.h"

#include <set>

namespace apptraverse {

OverlayDomainStorage::OverlayDomainStorage(ae::IDomainStorage& backing)
    : backing_{backing} {}

OverlayDomainStorage::~OverlayDomainStorage() = default;

std::unique_ptr<ae::IDomainStorageWriter> OverlayDomainStorage::Store(
    ae::DomainQuery const& query) {
  return overlay_.Store(query);
}

ae::ClassList OverlayDomainStorage::Enumerate(ae::ObjId const& obj_id) {
  std::set<std::uint32_t> classes;
  for (auto class_id : overlay_.Enumerate(obj_id)) {
    classes.insert(class_id);
  }
  for (auto class_id : backing_.Enumerate(obj_id)) {
    classes.insert(class_id);
  }
  return ae::ClassList{classes.begin(), classes.end()};
}

ae::DomainLoad OverlayDomainStorage::Load(ae::DomainQuery const& query) {
  auto overlay_load = overlay_.Load(query);
  if (overlay_load.result == ae::DomainLoadResult::kLoaded ||
      overlay_load.result == ae::DomainLoadResult::kRemoved) {
    return overlay_load;
  }
  return backing_.Load(query);
}

void OverlayDomainStorage::Remove(ae::ObjId const& obj_id) {
  // Static graph runtime does not use Remove; keep overlay-only minimal mark.
  overlay_.Remove(obj_id);
}

void OverlayDomainStorage::CleanUp() { overlay_.CleanUp(); }

}  // namespace apptraverse
