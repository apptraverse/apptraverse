#pragma once
#include <filesystem>
#include "aether-objects/domain_storage/ram_domain_storage.h"
namespace apptraverse {
// The storage itself is always RamDomainStorage. These explicit file operations
// are called only at application startup and shutdown.
bool LoadUserStorageFile(ae::RamDomainStorage& storage,
                         std::filesystem::path const& profile);
bool SaveUserStorageFile(ae::RamDomainStorage const& storage,
                         std::filesystem::path const& profile);
}
