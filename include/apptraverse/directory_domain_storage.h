#ifndef APPTRAVERSE_DIRECTORY_DOMAIN_STORAGE_H_
#define APPTRAVERSE_DIRECTORY_DOMAIN_STORAGE_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <unordered_set>

#include "aether-objects/obj/idomain_storage.h"

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

// Temporary profiling wrapper: Store/Load counts, bytes, unique ObjIds, and
// new vs overwritten layer writes (version already present on Enumerate).
struct InstrumentedDomainStorageStats {
  std::size_t store_calls{0};
  std::size_t load_calls{0};
  std::size_t store_bytes{0};
  std::size_t load_bytes{0};
  std::size_t store_new_layers{0};
  std::size_t store_overwrite_layers{0};
  std::unordered_set<std::uint32_t> store_obj_ids;
};

class InstrumentedDomainStorage final : public ae::IDomainStorage {
 public:
  explicit InstrumentedDomainStorage(ae::IDomainStorage& inner);

  std::unique_ptr<ae::IDomainStorageWriter> Store(
      ae::DomainQuery const& query) override;
  ae::ClassList Enumerate(ae::ObjId const& obj_id) override;
  ae::DomainLoad Load(ae::DomainQuery const& query) override;
  void Remove(ae::ObjId const& obj_id) override;
  void CleanUp() override;

  InstrumentedDomainStorageStats SnapshotStats() const;

 private:
  ae::IDomainStorage& inner_;
  mutable std::mutex mu_;
  InstrumentedDomainStorageStats stats_;
};

void LogInstrumentedDomainStorageStats(InstrumentedDomainStorageStats const& stats);

}  // namespace apptraverse

#endif  // APPTRAVERSE_DIRECTORY_DOMAIN_STORAGE_H_
