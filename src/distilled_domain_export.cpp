#include "apptraverse/distilled_domain_export.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <ios>
#include <vector>

namespace apptraverse {
namespace {

struct LayerKey {
  ae::ObjId::Type object_id{};
  std::uint32_t class_id{};
  std::uint8_t version{};
};

bool LayerKeyLess(LayerKey const& a, LayerKey const& b) {
  if (a.object_id != b.object_id) {
    return a.object_id < b.object_id;
  }
  if (a.class_id != b.class_id) {
    return a.class_id < b.class_id;
  }
  return a.version < b.version;
}

struct LayerExport {
  LayerKey key;
  ae::ObjectData bytes;
};

std::vector<LayerExport> CollectSortedLayers(
    ae::RamDomainStorage const& storage,
    std::set<ae::ObjId> const& object_filter) {
  std::vector<LayerExport> layers;
  for (auto const& [obj_id, class_map_opt] : storage.state) {
    if (object_filter.find(obj_id) == object_filter.end()) {
      continue;
    }
    if (!class_map_opt) {
      continue;
    }
    for (auto const& [class_id, version_map] : *class_map_opt) {
      for (auto const& [version, bytes] : version_map) {
        layers.push_back(
            LayerExport{.key = {.object_id = obj_id.id(),
                                .class_id = class_id,
                                .version = version},
                        .bytes = bytes});
      }
    }
  }
  std::sort(layers.begin(), layers.end(),
            [](LayerExport const& a, LayerExport const& b) {
              return LayerKeyLess(a.key, b.key);
            });
  return layers;
}

void WriteBytesArray(std::ostream& out, std::string_view name,
                     ae::ObjectData const& bytes) {
  out << "inline constexpr std::uint8_t " << name << "[] = {";
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (i % 12 == 0) {
      out << "\n    ";
    }
    out << "0x";
    char hex[3];
    std::snprintf(hex, sizeof hex, "%02x", bytes[i]);
    out << hex;
    if (i + 1 != bytes.size()) {
      out << ", ";
    }
  }
  if (!bytes.empty()) {
    out << "\n";
  }
  out << "};\n\n";
}

}  // namespace

std::set<ae::ObjId> CollectStoredObjectIds(
    ae::RamDomainStorage const& storage) {
  std::set<ae::ObjId> ids;
  for (auto const& [obj_id, class_map_opt] : storage.state) {
    if (class_map_opt) {
      ids.insert(obj_id);
    }
  }
  return ids;
}

bool WriteEmbeddedDistilledSupplyHeader(
    std::filesystem::path const& output_path,
    std::string_view graph_namespace, std::string_view header_guard,
    ae::ObjId root_id, std::vector<ae::ObjId> const& supply_constant_ids,
    ae::RamDomainStorage const& storage,
    std::set<ae::ObjId> const& object_filter) {
  std::vector<LayerExport> const layers =
      CollectSortedLayers(storage, object_filter);
  std::vector<ae::ObjId> sorted_supply = supply_constant_ids;
  std::sort(sorted_supply.begin(), sorted_supply.end(),
            [](ae::ObjId const& a, ae::ObjId const& b) { return a < b; });
  sorted_supply.erase(
      std::unique(sorted_supply.begin(), sorted_supply.end()),
      sorted_supply.end());

  std::filesystem::path const tmp_path = output_path.string() + ".tmp";
  std::ofstream out(tmp_path, std::ios::binary);
  if (!out) {
    return false;
  }

  out << "#ifndef " << header_guard << "\n";
  out << "#define " << header_guard << "\n\n";
  out << "#include <cstddef>\n";
  out << "#include <cstdint>\n\n";
  out << "#include \"apptraverse/embedded_distilled_domain.h\"\n\n";
  out << "namespace apptraverse {\nnamespace embedded_distilled {\n";
  out << "namespace " << graph_namespace << " {\n\n";
  out << "inline constexpr std::uint8_t kEmptyLayerByte = 0;\n\n";

  for (std::size_t i = 0; i < layers.size(); ++i) {
    if (!layers[i].bytes.empty()) {
      WriteBytesArray(out, "kLayerBytes" + std::to_string(i), layers[i].bytes);
    }
  }

  if (!sorted_supply.empty()) {
    out << "inline constexpr ae::ObjId::Type kSupplyConstantIds[] = {";
    for (std::size_t i = 0; i < sorted_supply.size(); ++i) {
      out << sorted_supply[i].id();
      if (i + 1 != sorted_supply.size()) {
        out << ", ";
      }
    }
    out << "};\n\n";
  }

  out << "inline constexpr apptraverse::EmbeddedDistilledLayerRecord "
         "kLayers[] = {\n";
  for (std::size_t i = 0; i < layers.size(); ++i) {
    LayerExport const& layer = layers[i];
    out << "    {" << layer.key.object_id << "u, " << layer.key.class_id
        << "u, static_cast<std::uint8_t>("
        << static_cast<unsigned>(layer.key.version) << "u), ";
    if (layer.bytes.empty()) {
      out << "&kEmptyLayerByte, 0},\n";
    } else {
      out << "kLayerBytes" << i << ", sizeof(kLayerBytes" << i << ")},\n";
    }
  }
  out << "};\n\n";

  out << "inline constexpr apptraverse::EmbeddedDistilledSupply "
         "kSupplyConstants = {";
  if (sorted_supply.empty()) {
    out << "nullptr, 0";
  } else {
    out << "kSupplyConstantIds, " << sorted_supply.size();
  }
  out << "};\n\n";

  out << "inline constexpr apptraverse::EmbeddedDistilledGraph "
         "kEmbeddedGraph = {"
      << root_id.id() << "u, kSupplyConstants, kLayers, " << layers.size()
      << "};\n\n";
  out << "}  // namespace " << graph_namespace << "\n";
  out << "}  // namespace embedded_distilled\n";
  out << "}  // namespace apptraverse\n\n";
  out << "#endif  // " << header_guard << "\n";

  out.close();
  if (!out) {
    return false;
  }
  std::error_code ec;
  std::filesystem::rename(tmp_path, output_path, ec);
  return !ec;
}

}  // namespace apptraverse
