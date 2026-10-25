#include "apptraverse/distilled_supply_package.h"

#include <algorithm>
#include <fstream>
#include <map>

#include "aether-miscpp/serialization/binary_archive.h"

#include "apptraverse/distilled_domain_export.h"

namespace ae::seri {

template <typename B>
struct Serializer<BinaryArchive<B>, apptraverse::DistilledSupplyPackageLayer> {
  using Layer = apptraverse::DistilledSupplyPackageLayer;
  using Archive = BinaryArchive<B>;

  SeriResult Seri(Archive& archive, Meta<Layer const> meta) const {
    TRY_RESULT(archive.Save(Meta{meta.value.object_id}));
    TRY_RESULT(archive.Save(Meta{meta.value.class_id}));
    TRY_RESULT(archive.Save(Meta{meta.value.version}));
    return archive.Save(Meta{meta.value.bytes});
  }

  SeriResult Deseri(Archive& archive, Meta<Layer> meta) const {
    TRY_RESULT(archive.Load(Meta{meta.value.object_id}));
    TRY_RESULT(archive.Load(Meta{meta.value.class_id}));
    TRY_RESULT(archive.Load(Meta{meta.value.version}));
    return archive.Load(Meta{meta.value.bytes});
  }
};

template <typename B>
struct Serializer<BinaryArchive<B>, apptraverse::DistilledSupplyPackage> {
  using Package = apptraverse::DistilledSupplyPackage;
  using Archive = BinaryArchive<B>;

  SeriResult Seri(Archive& archive, Meta<Package const> meta) const {
    TRY_RESULT(archive.Save(Meta{meta.value.target_supply_constant_ids}));
    return archive.Save(Meta{meta.value.layers});
  }

  SeriResult Deseri(Archive& archive, Meta<Package> meta) const {
    TRY_RESULT(archive.Load(Meta{meta.value.target_supply_constant_ids}));
    return archive.Load(Meta{meta.value.layers});
  }
};

}  // namespace ae::seri

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

std::optional<DistilledSupplyPackage> DeserializePackage(
    std::vector<std::uint8_t> const& bytes) {
  std::vector<std::uint8_t> copy = bytes;
  ae::seri::BinaryVectorBuffer buffer{copy};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  DistilledSupplyPackage package{};
  if (archive.Load(package).IsErr()) {
    return std::nullopt;
  }
  return package;
}

bool SerializePackage(DistilledSupplyPackage const& package,
                      std::vector<std::uint8_t>& out) {
  out.clear();
  ae::seri::BinaryVectorBuffer buffer{out};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  return archive.Save(package).IsOk();
}

}  // namespace

void ApplyDistilledSupplyPackage(ae::RamDomainStorage& effective_backing,
                                 DistilledSupplyPackage const& package) {
  for (auto const& layer : package.layers) {
    ae::DomainQuery const query{ae::ObjId{layer.object_id}, layer.class_id,
                                layer.version};
    ae::ObjectData bytes;
    bytes.insert(bytes.end(), layer.bytes.begin(), layer.bytes.end());
    effective_backing.SaveData(query, std::move(bytes));
  }
}

bool WriteDistilledSupplyPackageFile(
    std::filesystem::path const& path,
    DistilledSupplyPackage const& package) {
  std::vector<std::uint8_t> bytes;
  if (!SerializePackage(package, bytes)) {
    return false;
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out.write(reinterpret_cast<char const*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(out);
}

std::optional<DistilledSupplyPackage> ReadDistilledSupplyPackageFile(
    std::filesystem::path const& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }
  in.seekg(0, std::ios::end);
  auto const size = in.tellg();
  if (size < 0) {
    return std::nullopt;
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  in.seekg(0, std::ios::beg);
  in.read(reinterpret_cast<char*>(bytes.data()),
          static_cast<std::streamsize>(bytes.size()));
  if (!in) {
    return std::nullopt;
  }
  return DeserializePackage(bytes);
}

void BuildDistilledSupplyPackageFromStorageDiff(
    ae::RamDomainStorage const& base_storage,
    ae::RamDomainStorage const& target_storage,
    std::set<ae::ObjId> const& object_filter,
    std::vector<ae::ObjId> const& target_supply_constant_ids,
    DistilledSupplyPackage& out) {
  using LayerMap = std::map<LayerKey, ae::ObjectData, decltype(&LayerKeyLess)>;

  auto collect = [&](ae::RamDomainStorage const& storage) {
    LayerMap layers{LayerKeyLess};
    for (auto const& [obj_id, class_map_opt] : storage.state) {
      if (object_filter.find(obj_id) == object_filter.end() || !class_map_opt) {
        continue;
      }
      for (auto const& [class_id, version_map] : *class_map_opt) {
        for (auto const& [version, bytes] : version_map) {
          layers.insert({{obj_id.id(), class_id, version}, bytes});
        }
      }
    }
    return layers;
  };

  auto const base_layers = collect(base_storage);
  auto const target_layers = collect(target_storage);

  out = {};
  out.target_supply_constant_ids.reserve(target_supply_constant_ids.size());
  for (auto const& id : target_supply_constant_ids) {
    out.target_supply_constant_ids.push_back(id.id());
  }
  std::sort(out.target_supply_constant_ids.begin(),
            out.target_supply_constant_ids.end());

  for (auto const& [key, target_bytes] : target_layers) {
    auto base_it = base_layers.find(key);
    if (base_it != base_layers.end() && base_it->second == target_bytes) {
      continue;
    }
    DistilledSupplyPackageLayer layer{};
    layer.object_id = key.object_id;
    layer.class_id = key.class_id;
    layer.version = key.version;
    layer.bytes.assign(target_bytes.begin(), target_bytes.end());
    out.layers.push_back(std::move(layer));
  }

  std::sort(out.layers.begin(), out.layers.end(),
            [](DistilledSupplyPackageLayer const& a,
               DistilledSupplyPackageLayer const& b) {
              return LayerKeyLess({a.object_id, a.class_id, a.version},
                                  {b.object_id, b.class_id, b.version});
            });
}

}  // namespace apptraverse
