#ifndef APPTRAVERSE_DIRECTORY_DOMAIN_STORAGE_H_
#define APPTRAVERSE_DIRECTORY_DOMAIN_STORAGE_H_

#include <filesystem>

#include "aether/obj/idomain_storage.h"

namespace apptraverse {

// File-backed IDomainStorage rooted at an explicit directory (not CWD).
// Layout matches ae::FileSystemStdStorage: <root>/<obj_id>/<class_id>/<version>
class DirectoryDomainStorage final : public ae::IDomainStorage {
 public:
  explicit DirectoryDomainStorage(std::filesystem::path root);
  ~DirectoryDomainStorage() override;

  std::unique_ptr<ae::IDomainStorageWriter> Store(
      ae::DomainQuery const& query) override;
  ae::ClassList Enumerate(ae::ObjId const& obj_id) override;
  ae::DomainLoad Load(ae::DomainQuery const& query) override;
  void Remove(ae::ObjId const& obj_id) override;
  void CleanUp() override;

  std::filesystem::path const& root() const { return root_; }

 private:
  std::filesystem::path root_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_DIRECTORY_DOMAIN_STORAGE_H_
