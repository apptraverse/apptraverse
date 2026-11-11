#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>
#include "aether-objects/domain_storage/ram_domain_storage.h"
namespace apptraverse {
// The same USER representation used by user.bin, without filesystem access.
// Success replaces the output; a failed decode leaves storage unchanged.
bool SerializeUserStorage(ae::RamDomainStorage const& storage,
                          std::vector<std::uint8_t>& bytes);
bool DeserializeUserStorage(ae::RamDomainStorage& storage,
                            std::vector<std::uint8_t> const& bytes);

// The storage itself is always RamDomainStorage. These explicit file operations
// are called only at startup and the platform's persistence lifecycle boundary.
bool LoadUserStorageFile(ae::RamDomainStorage& storage,
                         std::filesystem::path const& profile);
bool SaveUserStorageFile(ae::RamDomainStorage const& storage,
                         std::filesystem::path const& profile);
}
