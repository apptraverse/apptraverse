#ifndef APPTRAVERSE_SINGLE_FILE_USER_STORAGE_H_
#define APPTRAVERSE_SINGLE_FILE_USER_STORAGE_H_

#include <filesystem>
#include <utility>
#include "aether-objects/domain_storage/ram_domain_storage.h"

namespace apptraverse {

// All domain operations are RAM-only, including lazy reload after window Close.
// The application explicitly loads once at startup and saves once at shutdown.
// Destruction never writes. The file contains USER layers, not BACKING.
class SingleFileUserStorage final : public ae::RamDomainStorage {
 public:
  explicit SingleFileUserStorage(std::filesystem::path profile_directory)
      : file_{std::move(profile_directory) / "user.bin"} {}
  bool LoadFile();  // Missing file is an empty USER; malformed/unreadable fails.
  bool SaveFile() const;
  std::filesystem::path const& file() const { return file_; }
 private:
  std::filesystem::path file_;
};

}  // namespace apptraverse
#endif
