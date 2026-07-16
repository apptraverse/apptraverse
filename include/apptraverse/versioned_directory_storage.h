#ifndef APPTRAVERSE_VERSIONED_DIRECTORY_STORAGE_H_
#define APPTRAVERSE_VERSIONED_DIRECTORY_STORAGE_H_

#include <filesystem>
#include <memory>
#include <vector>

#include "aether/obj/idomain_storage.h"

#include "apptraverse/directory_domain_storage.h"

namespace apptraverse {

class VersionedDirectoryStorage final : public ae::IDomainStorage {
 public:
  VersionedDirectoryStorage(
      std::filesystem::path writable_root,
      std::vector<std::filesystem::path> fallback_roots);
  ~VersionedDirectoryStorage() override;

  VersionedDirectoryStorage(VersionedDirectoryStorage const&) = delete;
  VersionedDirectoryStorage& operator=(VersionedDirectoryStorage const&) =
      delete;

  std::unique_ptr<ae::IDomainStorageWriter> Store(
      ae::DomainQuery const& query) override;
  ae::ClassList Enumerate(ae::ObjId const& obj_id) override;
  ae::DomainLoad Load(ae::DomainQuery const& query) override;
  void Remove(ae::ObjId const& obj_id) override;
  void CleanUp() override;

 private:
  DirectoryDomainStorage* ResolveObjectStorage(ae::ObjId const& obj_id);

  DirectoryDomainStorage writable_;
  std::vector<std::unique_ptr<DirectoryDomainStorage>> fallbacks_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_VERSIONED_DIRECTORY_STORAGE_H_
