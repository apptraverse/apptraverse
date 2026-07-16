#include "apptraverse/versioned_directory_storage.h"

#include <utility>

namespace apptraverse {

VersionedDirectoryStorage::VersionedDirectoryStorage(
    std::filesystem::path writable_root,
    std::vector<std::filesystem::path> fallback_roots)
    : writable_{std::move(writable_root)} {
  fallbacks_.reserve(fallback_roots.size());
  for (auto& root : fallback_roots) {
    fallbacks_.push_back(
        std::make_unique<DirectoryDomainStorage>(std::move(root)));
  }
}

VersionedDirectoryStorage::~VersionedDirectoryStorage() = default;

DirectoryDomainStorage* VersionedDirectoryStorage::ResolveObjectStorage(
    ae::ObjId const& obj_id) {
  if (writable_.HasObjectDirectory(obj_id)) {
    return &writable_;
  }

  for (auto& fallback : fallbacks_) {
    if (fallback->HasObjectDirectory(obj_id)) {
      return fallback.get();
    }
  }

  return nullptr;
}

std::unique_ptr<ae::IDomainStorageWriter> VersionedDirectoryStorage::Store(
    ae::DomainQuery const& query) {
  return writable_.Store(query);
}

ae::ClassList VersionedDirectoryStorage::Enumerate(ae::ObjId const& obj_id) {
  auto* const storage = ResolveObjectStorage(obj_id);
  if (storage == nullptr) {
    return {};
  }
  return storage->Enumerate(obj_id);
}

ae::DomainLoad VersionedDirectoryStorage::Load(ae::DomainQuery const& query) {
  auto* const storage = ResolveObjectStorage(query.id);
  if (storage == nullptr) {
    return {ae::DomainLoadResult::kEmpty, {}};
  }
  return storage->Load(query);
}

void VersionedDirectoryStorage::Remove(ae::ObjId const& obj_id) {
  writable_.Remove(obj_id);
}

void VersionedDirectoryStorage::CleanUp() { writable_.CleanUp(); }

}  // namespace apptraverse
