#ifndef APPTRAVERSE_DISTILLED_SUPPLY_PACKAGE_H_
#define APPTRAVERSE_DISTILLED_SUPPLY_PACKAGE_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj_id.h"

namespace apptraverse {

inline constexpr std::uint32_t kDistilledSupplyPackageMagic = 0x50505341u;  // 'APSP'
inline constexpr std::uint32_t kDistilledSupplyPackageFormatVersion = 0;

enum class DistilledSupplyPackageOp : std::uint8_t {
  kUpsert = 0,
  kRemove = 1,
};

struct DistilledSupplyPackageLayerKey {
  ae::ObjId::Type object_id{};
  std::uint32_t class_id{};
  std::uint8_t version{};
};

struct DistilledSupplyPackageOperation {
  DistilledSupplyPackageLayerKey key{};
  DistilledSupplyPackageOp op{};
  std::vector<std::uint8_t> bytes;
};

struct DistilledSupplyPackage {
  std::uint32_t base_release{};
  std::uint32_t target_release{};
  std::vector<ae::ObjId::Type> target_supply_constant_ids{};
  std::vector<DistilledSupplyPackageOperation> operations{};
};

struct DistilledSupplyPackageError {
  char const* message{};
};

std::optional<DistilledSupplyPackage> ParseDistilledSupplyPackage(
    std::span<std::uint8_t const> bytes,
    DistilledSupplyPackageError* error_out);

bool ValidateDistilledSupplyPackage(
    DistilledSupplyPackage const& package,
    std::set<ae::ObjId> const& base_supply_object_ids,
    std::set<ae::ObjId> const& base_layer_object_ids,
    DistilledSupplyPackageError* error_out);

bool ApplyDistilledSupplyPackage(
    ae::RamDomainStorage& effective_backing,
    DistilledSupplyPackage const& package,
    DistilledSupplyPackageError* error_out);

bool WriteDistilledSupplyPackageFile(
    std::filesystem::path const& path,
    DistilledSupplyPackage const& package,
    DistilledSupplyPackageError* error_out,
    bool require_canonical = true);

std::optional<std::vector<std::uint8_t>> ReadDistilledSupplyPackageFile(
    std::filesystem::path const& path,
    DistilledSupplyPackageError* error_out);

bool BuildDistilledSupplyPackageFromStorageDiff(
    ae::RamDomainStorage const& base_storage,
    ae::RamDomainStorage const& target_storage,
    std::set<ae::ObjId> const& object_filter,
    std::vector<ae::ObjId> const& target_supply_constant_ids,
    std::uint32_t base_release, std::uint32_t target_release,
    DistilledSupplyPackage& out, DistilledSupplyPackageError* error_out);

}  // namespace apptraverse

#endif  // APPTRAVERSE_DISTILLED_SUPPLY_PACKAGE_H_
