#ifndef APPTRAVERSE_DIRECTORY_DOMAIN_STORAGE_H_
#define APPTRAVERSE_DIRECTORY_DOMAIN_STORAGE_H_

#include <filesystem>
#include <memory>

#include "aether/obj/idomain_storage.h"

namespace apptraverse {

class VersionedDirectoryStorage;

class DirectoryDomainStorage final : public ae::IDomainStorage {
 public:
  explicit DirectoryDomainStorage(std::filesystem::path root);
  ~DirectoryDomainStorage() override;

  DirectoryDomainStorage(DirectoryDomainStorage const&) = delete;
  DirectoryDomainStorage& operator=(DirectoryDomainStorage const&) = delete;

  std::unique_ptr<ae::IDomainStorageWriter> Store(
      ae::DomainQuery const& query) override;
  ae::ClassList Enumerate(ae::ObjId const& obj_id) override;
  ae::DomainLoad Load(ae::DomainQuery const& query) override;
  void Remove(ae::ObjId const& obj_id) override;
  void CleanUp() override;

 private:
  friend class VersionedDirectoryStorage;

  bool HasObjectDirectory(ae::ObjId const& obj_id) const;

  std::filesystem::path ObjectDir(ae::ObjId const& obj_id) const;
  std::filesystem::path ClassDir(ae::DomainQuery const& query) const;
  std::filesystem::path VersionPath(ae::DomainQuery const& query) const;

  std::filesystem::path root_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_DIRECTORY_DOMAIN_STORAGE_H_
