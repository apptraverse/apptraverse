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
  std::size_t store_skipped_unchanged_layers{0};
  std::unordered_set<std::uint32_t> store_obj_ids;
};

// Optional counters while DirectoryDomainStorage commits layer files (e.g. during
// Application::Save). Not thread-safe; set on one thread around Save only.
struct DirectoryDomainStorageStoreStats {
  std::size_t layer_store_calls{0};
  std::size_t layers_written{0};
  std::size_t layers_skipped_unchanged{0};
  std::size_t layers_written_bytes{0};
};

void SetDirectoryDomainStorageStoreStats(DirectoryDomainStorageStoreStats* stats);
DirectoryDomainStorageStoreStats TakeDirectoryDomainStorageStoreStats();

std::filesystem::path DirectoryDomainStorageLayerPath(
    std::filesystem::path const& root, ae::DomainQuery const& query);

class InstrumentedDomainStorage final : public ae::IDomainStorage {
 public:
  explicit InstrumentedDomainStorage(ae::IDomainStorage& inner);
  InstrumentedDomainStorage(ae::IDomainStorage& inner,
                            std::filesystem::path const& layer_root);

  std::unique_ptr<ae::IDomainStorageWriter> Store(
      ae::DomainQuery const& query) override;
  ae::ClassList Enumerate(ae::ObjId const& obj_id) override;
  ae::DomainLoad Load(ae::DomainQuery const& query) override;
  void Remove(ae::ObjId const& obj_id) override;
  void CleanUp() override;

  InstrumentedDomainStorageStats SnapshotStats() const;

 private:
  ae::IDomainStorage& inner_;
  std::filesystem::path const* layer_root_{nullptr};
  mutable std::mutex mu_;
  InstrumentedDomainStorageStats stats_;
};

void LogInstrumentedDomainStorageStats(InstrumentedDomainStorageStats const& stats);

bool DirectoryDomainStorageLayerFileExists(std::filesystem::path const& root,
                                           ae::DomainQuery const& query);

}  // namespace apptraverse

#endif  // APPTRAVERSE_DIRECTORY_DOMAIN_STORAGE_H_
