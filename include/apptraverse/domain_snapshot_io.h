#ifndef APPTRAVERSE_DOMAIN_SNAPSHOT_IO_H_
#define APPTRAVERSE_DOMAIN_SNAPSHOT_IO_H_

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "aether/domain_storage/ram_domain_storage.h"

namespace apptraverse {

struct DomainSnapshotIoStats {
  std::uint64_t load_calls{0};
  std::uint64_t save_calls{0};
  std::uint64_t last_load_files{0};
  std::uint64_t last_load_bytes{0};
  std::uint64_t last_save_files{0};
  std::uint64_t last_save_bytes{0};
};

DomainSnapshotIoStats& GetDomainSnapshotIoStats();
void ResetDomainSnapshotIoStats();

using DomainSnapshotMarkerFn = std::function<void(std::string const& marker)>;

// Optional diagnostic sink for MODEL_SNAPSHOT_* markers (tests / Windows runtime).
void SetDomainSnapshotMarkerSink(DomainSnapshotMarkerFn sink);

// Import `<obj_id>/<class_id>/<version>` directory layout into RAM.
// Empty/missing directory leaves `ram` unchanged (typically empty).
// Empty object directories (no class children) become Remove() tombstones.
void LoadDirectorySnapshot(std::filesystem::path const& directory,
                           ae::RamDomainStorage& ram);

// Replace `directory` with a full dump of `ram` (existing objects only;
// tombstones become empty object directories matching DirectoryDomainStorage::Remove).
void SaveDirectorySnapshot(ae::RamDomainStorage const& ram,
                           std::filesystem::path const& directory);

}  // namespace apptraverse

#endif  // APPTRAVERSE_DOMAIN_SNAPSHOT_IO_H_
