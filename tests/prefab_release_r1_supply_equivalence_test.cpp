#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <tuple>
#include <vector>

#include "apptraverse/distilled_supply_package.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "embedded_distilled_prefab_release_r0_supply.h"
#include "embedded_distilled_prefab_release_r1_supply.h"

namespace {

struct LayerKey {
  ae::ObjId::Type object_id{};
  std::uint32_t class_id{};
  std::uint8_t version{};

  auto operator<=>(LayerKey const&) const = default;
};

using LayerMap = std::map<LayerKey, ae::ObjectData>;

LayerMap CollectLayers(ae::RamDomainStorage const& storage) {
  LayerMap out;
  for (auto const& [obj_id, class_map_opt] : storage.state) {
    if (!class_map_opt) {
      continue;
    }
    for (auto const& [class_id, version_map] : *class_map_opt) {
      for (auto const& [version, bytes] : version_map) {
        out.insert({{obj_id.id(), class_id, version}, bytes});
      }
    }
  }
  return out;
}

std::vector<ae::ObjId::Type> SortedSupplyIds(
    apptraverse::EmbeddedDistilledGraph const& graph) {
  std::vector<ae::ObjId::Type> ids;
  ids.reserve(graph.supply_constant_ids.count);
  for (std::size_t i = 0; i < graph.supply_constant_ids.count; ++i) {
    ids.push_back(graph.supply_constant_ids.ids[i]);
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: prefab_release_r1_supply_equivalence_test <r1-supply.bin>\n";
    return 2;
  }

  auto const package = apptraverse::ReadDistilledSupplyPackageFile(argv[1]);
  if (!package) {
    std::cerr << "package read failed\n";
    return 1;
  }

  ae::RamDomainStorage path_a;
  apptraverse::InstallEmbeddedDistilledBacking(
      path_a,
      apptraverse::embedded_distilled::prefab_release_r0::kEmbeddedGraph);
  apptraverse::ApplyDistilledSupplyPackage(path_a, *package);

  ae::RamDomainStorage path_b;
  apptraverse::InstallEmbeddedDistilledBacking(
      path_b,
      apptraverse::embedded_distilled::prefab_release_r1::kEmbeddedGraph);

  LayerMap const layers_a = CollectLayers(path_a);
  LayerMap const layers_b = CollectLayers(path_b);
  if (layers_a != layers_b) {
    std::cerr << "layer map mismatch\n";
    return 1;
  }

  auto const embed_supply = SortedSupplyIds(
      apptraverse::embedded_distilled::prefab_release_r1::kEmbeddedGraph);
  auto package_supply = package->target_supply_constant_ids;
  std::sort(package_supply.begin(), package_supply.end());
  if (embed_supply != package_supply) {
    std::cerr << "supply id set mismatch\n";
    return 1;
  }

  return 0;
}
