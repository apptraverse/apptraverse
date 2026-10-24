#ifndef APPTRAVERSE_DIRECTORY_DOMAIN_STORAGE_H_
#define APPTRAVERSE_DIRECTORY_DOMAIN_STORAGE_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>

#include "aether-objects/obj/idomain_storage.h"

namespace apptraverse {

struct DirectoryDomainStorageStoreStats {
  std::size_t layer_store_calls{0};
  std::size_t layers_bytes_accepted{0};
  std::size_t layers_written{0};
  std::size_t layers_skipped_unchanged{0};
  std::size_t layers_written_bytes{0};
  std::size_t layers_failed_commits{0};
};

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

  void BeginSaveSession();
  DirectoryDomainStorageStoreStats SnapshotSaveSessionStats() const;
  bool SaveSessionHadFailure() const;

  enum class LayerCommitOutcome : std::uint8_t {
    kNone,
    kSkippedUnchanged,
    kWritten,
    kFailed,
  };
  LayerCommitOutcome TakeLastLayerCommitOutcome();

  void RecordLayerBytesAccepted(std::size_t bytes);
  void RecordLayerCommit(LayerCommitOutcome outcome, std::size_t disk_bytes);

 private:
  std::filesystem::path root_;
  DirectoryDomainStorageStoreStats save_session_stats_{};
  bool save_session_active_{false};
  bool save_session_had_failure_{false};
  LayerCommitOutcome last_layer_commit_{LayerCommitOutcome::kNone};
};

std::filesystem::path DirectoryDomainStorageLayerPath(
    std::filesystem::path const& root, ae::DomainQuery const& query);

}  // namespace apptraverse

#endif  // APPTRAVERSE_DIRECTORY_DOMAIN_STORAGE_H_
