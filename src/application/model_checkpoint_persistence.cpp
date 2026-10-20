#include "apptraverse/application/model_checkpoint_persistence.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/object_serialization.h"

namespace apptraverse {

ModelCheckpointPaths ModelCheckpointPathsForStateDir(
    std::filesystem::path const& state_dir) {
  ModelCheckpointPaths paths;
  paths.state_dir = state_dir;
  paths.active = state_dir / kModelCheckpointFileName;
  paths.temp = paths.active.string() + kModelCheckpointTempSuffix;
  return paths;
}

bool HasActiveModelCheckpoint(ModelCheckpointPaths const& paths) {
  std::error_code ec;
  return std::filesystem::is_regular_file(paths.active, ec) && !ec;
}

std::uint64_t ActiveModelCheckpointByteSize(
    ModelCheckpointPaths const& paths) {
  std::error_code ec;
  auto const size = std::filesystem::file_size(paths.active, ec);
  if (ec) {
    return 0;
  }
  return static_cast<std::uint64_t>(size);
}

std::size_t CountRegularFilesUnder(std::filesystem::path const& root) {
  std::error_code ec;
  if (!std::filesystem::exists(root, ec)) {
    return 0;
  }
  std::size_t count = 0;
  for (auto const& entry :
       std::filesystem::recursive_directory_iterator(root, ec)) {
    if (ec) {
      break;
    }
    if (entry.is_regular_file(ec) && !ec) {
      ++count;
    }
  }
  return count;
}

namespace {

bool WriteCheckpointBytesAtomically(std::filesystem::path const& temp_path,
                                    std::filesystem::path const& active_path,
                                    std::vector<std::uint8_t> const& bytes) {
  std::error_code ec;
  std::filesystem::create_directories(active_path.parent_path(), ec);
  if (ec) {
    return false;
  }
  {
    std::ofstream out{temp_path,
                      std::ios::out | std::ios::binary | std::ios::trunc};
    if (!out) {
      return false;
    }
    if (!bytes.empty()) {
      out.write(reinterpret_cast<char const*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
      if (!out) {
        std::filesystem::remove(temp_path, ec);
        return false;
      }
    }
    out.flush();
    if (!out) {
      std::filesystem::remove(temp_path, ec);
      return false;
    }
  }
  ec.clear();
  std::filesystem::rename(temp_path, active_path, ec);
  if (ec) {
    std::filesystem::remove(temp_path, ec);
    return false;
  }
  ec.clear();
  auto const written = std::filesystem::file_size(active_path, ec);
  if (ec || written != bytes.size()) {
    return false;
  }
  return true;
}

}  // namespace

bool CommitModelCheckpoint(ae::Obj& root, ModelCheckpointPaths const& paths) {
  using Clock = std::chrono::steady_clock;
  char const* profile_env = std::getenv("APPTRAVERSE_CHECKPOINT_COMMIT_PROFILE");
  bool const profile =
      profile_env != nullptr && profile_env[0] == '1' && profile_env[1] == '\0';

  ByteSink bytes;
  auto const serialize_t0 = profile ? Clock::now() : Clock::time_point{};
  SerializePersistentModelSnapshot(root, bytes);
  auto const serialize_ms =
      profile ? std::chrono::duration_cast<std::chrono::milliseconds>(
                    Clock::now() - serialize_t0)
                    .count()
              : 0;

  auto const write_t0 = profile ? Clock::now() : Clock::time_point{};
  bool const ok =
      WriteCheckpointBytesAtomically(paths.temp, paths.active, bytes.bytes);
  if (profile) {
    auto const write_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              Clock::now() - write_t0)
                              .count();
    std::cout << "checkpoint_commit_profile serialize_ms=" << serialize_ms
              << " write_ms=" << write_ms << " bytes=" << bytes.bytes.size()
              << " ok=" << (ok ? 1 : 0) << '\n';
  }
  return ok;
}

bool LoadModelCheckpoint(ModelCheckpointPaths const& paths, ae::Domain& domain,
                         ae::IDomainStorage& layer_storage, ae::Obj& root) {
  std::ifstream in{paths.active, std::ios::binary | std::ios::ate};
  if (!in) {
    return false;
  }
  auto const size = static_cast<std::size_t>(in.tellg());
  in.seekg(0);
  std::vector<std::uint8_t> file_bytes(size);
  if (size > 0) {
    in.read(reinterpret_cast<char*>(file_bytes.data()),
            static_cast<std::streamsize>(size));
    if (!in) {
      return false;
    }
  }
  ByteSource checkpoint;
  checkpoint.data = file_bytes.data();
  checkpoint.size = file_bytes.size();
  checkpoint.pos = 0;
  LoadPersistentModelSnapshot(checkpoint, domain, layer_storage, root);
  return true;
}

}  // namespace apptraverse
