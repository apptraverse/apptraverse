#ifndef APPTRAVERSE_APPLICATION_MODEL_CHECKPOINT_PERSISTENCE_H_
#define APPTRAVERSE_APPLICATION_MODEL_CHECKPOINT_PERSISTENCE_H_

#include <cstdint>
#include <filesystem>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/idomain_storage.h"
#include "aether-objects/obj/obj.h"

namespace apptraverse {

class DirectoryDomainStorage;

inline constexpr char kModelCheckpointFileName[] = "model_checkpoint.bin";
inline constexpr char kModelCheckpointTempSuffix[] = ".tmp";

struct ModelCheckpointPaths {
  std::filesystem::path state_dir;
  std::filesystem::path active;
  std::filesystem::path temp;
};

ModelCheckpointPaths ModelCheckpointPathsForStateDir(
    std::filesystem::path const& state_dir);

bool HasActiveModelCheckpoint(ModelCheckpointPaths const& paths);

// Serialize root to bytes and atomically replace active checkpoint (temp write,
// flush/close, rename). Previous active file is kept if any step fails.
bool CommitModelCheckpoint(ae::Obj& root, ModelCheckpointPaths const& paths);

// Load checkpoint bytes into layer_storage and hydrate root (full persistent
// snapshot semantics). Does not create per-layer directory files.
bool LoadModelCheckpoint(ModelCheckpointPaths const& paths, ae::Domain& domain,
                         ae::IDomainStorage& layer_storage, ae::Obj& root);

std::uint64_t ActiveModelCheckpointByteSize(
    ModelCheckpointPaths const& paths);

// Count regular files under state_dir (diagnostic; checkpoint backend should
// stay O(1) regardless of journal length).
std::size_t CountRegularFilesUnder(std::filesystem::path const& root);

enum class ModelDurablePersistence {
  // Per-object layer files under state_dir (legacy / diagnostic).
  DirectoryObjectLayers,
  // Single model_checkpoint.bin; working set in RAM during the session.
  PackedCheckpointFile,
};

struct ModelPublicationSaveContext {
  ModelDurablePersistence backend{ModelDurablePersistence::PackedCheckpointFile};
  bool success{false};
  DirectoryDomainStorage* directory_storage{nullptr};
  ModelCheckpointPaths checkpoint_paths;
  std::uint64_t checkpoint_bytes{0};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_APPLICATION_MODEL_CHECKPOINT_PERSISTENCE_H_
