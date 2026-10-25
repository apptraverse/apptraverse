#ifndef APPTRAVERSE_EMBEDDED_DISTILLED_DOMAIN_H_
#define APPTRAVERSE_EMBEDDED_DISTILLED_DOMAIN_H_

#include <cstddef>
#include <cstdint>
#include <set>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj_id.h"

namespace apptraverse {

struct EmbeddedDistilledLayerRecord {
  ae::ObjId::Type object_id{};
  std::uint32_t class_id{};
  std::uint8_t version{};
  std::uint8_t const* bytes{};
  std::size_t size{};
};

struct EmbeddedDistilledSupply {
  ae::ObjId::Type const* ids{};
  std::size_t count{};
};

struct EmbeddedDistilledGraph {
  ae::ObjId::Type root_object_id{};
  EmbeddedDistilledSupply supply_constant_ids{};
  EmbeddedDistilledLayerRecord const* layers{};
  std::size_t layer_count{};
};

void InstallEmbeddedDistilledBacking(ae::RamDomainStorage& backing,
                                     EmbeddedDistilledGraph const& graph);

std::set<ae::ObjId> EmbeddedSupplyConstantIdSet(
    EmbeddedDistilledGraph const& graph);

}  // namespace apptraverse

#endif  // APPTRAVERSE_EMBEDDED_DISTILLED_DOMAIN_H_
