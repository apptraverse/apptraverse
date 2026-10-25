#ifndef APPTRAVERSE_DISTILLED_SUPPLY_PACKAGE_H_
#define APPTRAVERSE_DISTILLED_SUPPLY_PACKAGE_H_

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj_id.h"

namespace apptraverse {

struct DistilledSupplyPackageLayer {
  ae::ObjId::Type object_id{};
  std::uint32_t class_id{};
  std::uint8_t version{};
  std::vector<std::uint8_t> bytes;
};

struct DistilledSupplyPackage {
  std::vector<ae::ObjId::Type> target_supply_constant_ids{};
  std::vector<DistilledSupplyPackageLayer> layers{};
};

void ApplyDistilledSupplyPackage(ae::RamDomainStorage& effective_backing,
                                 DistilledSupplyPackage const& package);

bool WriteDistilledSupplyPackageFile(
    std::filesystem::path const& path,
    DistilledSupplyPackage const& package);

std::optional<DistilledSupplyPackage> ReadDistilledSupplyPackageFile(
    std::filesystem::path const& path);

void BuildDistilledSupplyPackageFromStorageDiff(
    ae::RamDomainStorage const& base_storage,
    ae::RamDomainStorage const& target_storage,
    std::set<ae::ObjId> const& object_filter,
    std::vector<ae::ObjId> const& target_supply_constant_ids,
    DistilledSupplyPackage& out);

}  // namespace apptraverse

#endif  // APPTRAVERSE_DISTILLED_SUPPLY_PACKAGE_H_
