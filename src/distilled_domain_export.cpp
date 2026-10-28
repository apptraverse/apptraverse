#include "apptraverse/distilled_domain_export.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <ios>
#include <vector>

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/object_serialization.h"

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

bool WriteBytesArray(std::ostream& out, std::string_view name,
                     ae::ObjectData const& bytes) {
  if (bytes.empty()) {
    return false;
  }
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
  return true;
}

}  // namespace

std::set<ae::ObjId> CollectReachableObjectIds(ae::Obj& root) {
  return CollectPersistedSerializationObjectIds(root);
}

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

bool LoadDirectoryDomainStorageIntoRam(std::filesystem::path const& root,
                                       ae::RamDomainStorage& out) {
  std::error_code ec;
  if (!std::filesystem::is_directory(root, ec) || ec) {
    return false;
  }
  for (auto const& obj_entry : std::filesystem::directory_iterator(root, ec)) {
    if (ec || !obj_entry.is_directory()) {
      continue;
    }
    ae::ObjId::Type const object_id = static_cast<ae::ObjId::Type>(
        std::stoul(obj_entry.path().filename().string()));
    for (auto const& class_entry :
         std::filesystem::directory_iterator(obj_entry.path(), ec)) {
      if (ec || !class_entry.is_directory()) {
        continue;
      }
      std::uint32_t const class_id = static_cast<std::uint32_t>(
          std::stoul(class_entry.path().filename().string()));
      for (auto const& version_entry :
           std::filesystem::directory_iterator(class_entry.path(), ec)) {
        if (ec || !version_entry.is_regular_file()) {
          continue;
        }
        std::uint8_t const version = static_cast<std::uint8_t>(
            std::stoul(version_entry.path().filename().string()));
        std::ifstream in{version_entry.path(), std::ios::binary | std::ios::ate};
        if (!in) {
          return false;
        }
        auto const size = static_cast<std::size_t>(in.tellg());
        in.seekg(0);
        ae::ObjectData bytes(size);
        if (size > 0) {
          in.read(reinterpret_cast<char*>(bytes.data()),
                  static_cast<std::streamsize>(size));
          if (!in) {
            return false;
          }
        }
        ae::DomainQuery const query{ae::ObjId{object_id}, class_id, version};
        out.SaveData(query, std::move(bytes));
      }
    }
  }
  return true;
}

bool WriteEmbeddedDistilledSupplyHeader(
    std::filesystem::path const& output_path,
    std::string_view graph_namespace, std::string_view header_guard,
    ae::ObjId root_id, std::vector<ae::ObjId> const& supply_constant_ids,
    ae::RamDomainStorage const& storage,
    std::set<ae::ObjId> const& object_filter) {
  std::vector<LayerExport> const layers = CollectSortedLayers(storage, object_filter);
  std::vector<ae::ObjId> sorted_supply = supply_constant_ids;
  std::sort(sorted_supply.begin(), sorted_supply.end(),
            [](ae::ObjId const& a, ae::ObjId const& b) { return a < b; });
  sorted_supply.erase(
      std::unique(sorted_supply.begin(), sorted_supply.end()),
      sorted_supply.end());

  std::filesystem::path const tmp = output_path;
  std::filesystem::path const tmp_path = tmp.string() + ".tmp";
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
    WriteBytesArray(out, "kLayerBytes" + std::to_string(i), layers[i].bytes);
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
      << root_id.id() << "u, kSupplyConstants, kLayers, "
      << layers.size() << "};\n\n";
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

bool WriteEmbeddedDistilledGraphToDirectory(
    std::filesystem::path const& root, EmbeddedDistilledGraph const& graph) {
  for (std::size_t i = 0; i < graph.layer_count; ++i) {
    EmbeddedDistilledLayerRecord const& layer = graph.layers[i];
    std::filesystem::path const path =
        root / std::to_string(layer.object_id) /
        std::to_string(layer.class_id) /
        std::to_string(static_cast<unsigned>(layer.version));
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      return false;
    }
    auto const tmp = path.string() + ".tmp";
    std::filesystem::path const tmp_path{tmp};
    {
      std::ofstream out{tmp_path,
                        std::ios::out | std::ios::binary | std::ios::trunc};
      if (!out) {
        return false;
      }
      if (layer.size != 0) {
        out.write(reinterpret_cast<char const*>(layer.bytes),
                  static_cast<std::streamsize>(layer.size));
        if (!out) {
          std::filesystem::remove(tmp_path, ec);
          return false;
        }
      }
      out.flush();
      if (!out) {
        std::filesystem::remove(tmp_path, ec);
        return false;
      }
    }
    ec.clear();
    std::filesystem::rename(tmp_path, path, ec);
    if (ec) {
      std::filesystem::remove(tmp_path, ec);
      return false;
    }
  }
  return true;
}

}  // namespace apptraverse
