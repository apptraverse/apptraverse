#include "apptraverse/distilled_supply_package.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

#include "aether-miscpp/crc.h"

#include "apptraverse/distilled_domain_export.h"

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

bool LayerKeyEqual(LayerKey const& a, LayerKey const& b) {
  return a.object_id == b.object_id && a.class_id == b.class_id &&
         a.version == b.version;
}

struct Reader {
  std::span<std::uint8_t const> data;
  std::size_t offset{0};

  bool Read(void* out, std::size_t size) {
    if (offset + size > data.size()) {
      return false;
    }
    std::memcpy(out, data.data() + offset, size);
    offset += size;
    return true;
  }

  bool ReadU32(std::uint32_t& out) {
    std::array<std::uint8_t, 4> raw{};
    if (!Read(raw.data(), 4)) {
      return false;
    }
    out = static_cast<std::uint32_t>(raw[0]) |
          (static_cast<std::uint32_t>(raw[1]) << 8) |
          (static_cast<std::uint32_t>(raw[2]) << 16) |
          (static_cast<std::uint32_t>(raw[3]) << 24);
    return true;
  }

  bool ReadU8(std::uint8_t& out) { return Read(&out, 1); }
};

struct Writer {
  std::vector<std::uint8_t> data;

  void Write(void const* src, std::size_t size) {
    auto const* bytes = static_cast<std::uint8_t const*>(src);
    data.insert(data.end(), bytes, bytes + size);
  }

  void WriteU32(std::uint32_t value) {
    std::array<std::uint8_t, 4> raw{
        static_cast<std::uint8_t>(value & 0xff),
        static_cast<std::uint8_t>((value >> 8) & 0xff),
        static_cast<std::uint8_t>((value >> 16) & 0xff),
        static_cast<std::uint8_t>((value >> 24) & 0xff)};
    Write(raw.data(), 4);
  }

  void WriteU8(std::uint8_t value) { Write(&value, 1); }
};

bool IsSortedUniqueSupplyIds(std::vector<ae::ObjId::Type> const& ids) {
  for (std::size_t i = 1; i < ids.size(); ++i) {
    if (ids[i] <= ids[i - 1]) {
      return false;
    }
  }
  return true;
}

bool IsSortedUniqueOps(std::vector<DistilledSupplyPackageOperation> const& ops) {
  for (std::size_t i = 1; i < ops.size(); ++i) {
    LayerKey const a{ops[i - 1].key.object_id, ops[i - 1].key.class_id,
                     ops[i - 1].key.version};
    LayerKey const b{ops[i].key.object_id, ops[i].key.class_id,
                     ops[i].key.version};
    if (!LayerKeyLess(a, b)) {
      return false;
    }
  }
  return true;
}

std::set<ae::ObjId> SupplyIdSet(
    std::vector<ae::ObjId::Type> const& ids) {
  std::set<ae::ObjId> out;
  for (auto id : ids) {
    out.insert(ae::ObjId{id});
  }
  return out;
}

void RemoveLayerFromRam(ae::RamDomainStorage& storage,
                        ae::DomainQuery const& query) {
  auto obj_it = storage.state.find(query.id);
  if (obj_it == storage.state.end() || !obj_it->second) {
    return;
  }
  ae::RamDomainStorage::ClassData& class_map = *obj_it->second;
  auto class_it = class_map.find(query.class_id);
  if (class_it == class_map.end()) {
    return;
  }
  class_it->second.erase(query.version);
  if (class_it->second.empty()) {
    class_map.erase(class_it);
  }
  if (class_map.empty()) {
    storage.state.erase(obj_it);
  }
}

}  // namespace

std::optional<DistilledSupplyPackage> ParseDistilledSupplyPackage(
    std::span<std::uint8_t const> bytes,
    DistilledSupplyPackageError* error_out) {
  auto fail = [&](char const* msg) {
    if (error_out != nullptr) {
      error_out->message = msg;
    }
    return std::optional<DistilledSupplyPackage>{};
  };

  if (bytes.size() < 4 * 6) {
    return fail("truncated");
  }

  std::uint32_t const stored_crc =
      static_cast<std::uint32_t>(bytes[bytes.size() - 4]) |
      (static_cast<std::uint32_t>(bytes[bytes.size() - 3]) << 8) |
      (static_cast<std::uint32_t>(bytes[bytes.size() - 2]) << 16) |
      (static_cast<std::uint32_t>(bytes[bytes.size() - 1]) << 24);
  std::span<std::uint8_t const> body = bytes.subspan(0, bytes.size() - 4);
  std::uint32_t const computed_crc =
      static_cast<std::uint32_t>(crc32::from_buffer(body.data(), body.size()));
  if (stored_crc != computed_crc) {
    return fail("crc-mismatch");
  }

  Reader reader{body};
  std::uint32_t magic{};
  std::uint32_t format_version{};
  if (!reader.ReadU32(magic) || magic != kDistilledSupplyPackageMagic) {
    return fail("bad-magic");
  }
  if (!reader.ReadU32(format_version) ||
      format_version != kDistilledSupplyPackageFormatVersion) {
    return fail("bad-format-version");
  }

  DistilledSupplyPackage package{};
  if (!reader.ReadU32(package.base_release) ||
      !reader.ReadU32(package.target_release)) {
    return fail("truncated-header");
  }

  std::uint32_t supply_count{};
  if (!reader.ReadU32(supply_count) || supply_count > 1'000'000) {
    return fail("bad-supply-count");
  }
  package.target_supply_constant_ids.reserve(supply_count);
  for (std::uint32_t i = 0; i < supply_count; ++i) {
    std::uint32_t id{};
    if (!reader.ReadU32(id)) {
      return fail("truncated-supply-ids");
    }
    package.target_supply_constant_ids.push_back(id);
  }

  std::uint32_t op_count{};
  if (!reader.ReadU32(op_count) || op_count > 1'000'000) {
    return fail("bad-op-count");
  }
  package.operations.reserve(op_count);
  for (std::uint32_t i = 0; i < op_count; ++i) {
    DistilledSupplyPackageOperation op{};
    std::uint32_t object_id{};
    std::uint32_t class_id{};
    std::uint8_t version{};
    std::uint8_t op_code{};
    if (!reader.ReadU32(object_id) || !reader.ReadU32(class_id) ||
        !reader.ReadU8(version) || !reader.ReadU8(op_code)) {
      return fail("truncated-op-header");
    }
    op.key = {object_id, class_id, version};
    if (op_code == static_cast<std::uint8_t>(DistilledSupplyPackageOp::kUpsert)) {
      op.op = DistilledSupplyPackageOp::kUpsert;
      std::uint32_t byte_count{};
      if (!reader.ReadU32(byte_count) || byte_count > 16 * 1024 * 1024) {
        return fail("bad-upsert-size");
      }
      op.bytes.resize(byte_count);
      if (byte_count > 0 && !reader.Read(op.bytes.data(), byte_count)) {
        return fail("truncated-upsert-bytes");
      }
    } else if (op_code ==
               static_cast<std::uint8_t>(DistilledSupplyPackageOp::kRemove)) {
      op.op = DistilledSupplyPackageOp::kRemove;
    } else {
      return fail("bad-op-code");
    }
    package.operations.push_back(std::move(op));
  }

  if (reader.offset != body.size()) {
    return fail("trailing-garbage");
  }

  if (!IsSortedUniqueSupplyIds(package.target_supply_constant_ids)) {
    return fail("supply-ids-not-sorted-unique");
  }
  if (!IsSortedUniqueOps(package.operations)) {
    return fail("ops-not-sorted-unique");
  }

  return package;
}

bool ValidateDistilledSupplyPackage(
    DistilledSupplyPackage const& package,
    std::set<ae::ObjId> const& base_supply_object_ids,
    std::set<ae::ObjId> const& base_layer_object_ids,
    DistilledSupplyPackageError* error_out) {
  auto fail = [&](char const* msg) {
    if (error_out != nullptr) {
      error_out->message = msg;
    }
    return false;
  };

  if (package.target_release <= package.base_release) {
    return fail("target-not-after-base");
  }

  auto const target_supply = SupplyIdSet(package.target_supply_constant_ids);
  if (target_supply.size() != package.target_supply_constant_ids.size()) {
    return fail("supply-ids-not-unique");
  }

  for (auto const& op : package.operations) {
    ae::ObjId const obj_id{op.key.object_id};
    bool const in_base_supply =
        base_supply_object_ids.find(obj_id) != base_supply_object_ids.end();
    bool const in_target_supply =
        target_supply.find(obj_id) != target_supply.end();
    bool const in_base_layers =
        base_layer_object_ids.find(obj_id) != base_layer_object_ids.end();
    if (!in_base_supply && !in_target_supply && !in_base_layers) {
      return fail("op-outside-supply");
    }
    if (op.op == DistilledSupplyPackageOp::kRemove && !op.bytes.empty()) {
      return fail("remove-has-bytes");
    }
    if (op.op == DistilledSupplyPackageOp::kUpsert && op.bytes.empty()) {
      return fail("upsert-empty");
    }
  }

  return true;
}

bool ApplyDistilledSupplyPackage(
    ae::RamDomainStorage& effective_backing,
    DistilledSupplyPackage const& package,
    DistilledSupplyPackageError* error_out) {
  for (auto const& op : package.operations) {
    ae::DomainQuery const query{ae::ObjId{op.key.object_id}, op.key.class_id,
                                op.key.version};
    if (op.op == DistilledSupplyPackageOp::kUpsert) {
      ae::ObjectData bytes;
      bytes.insert(bytes.end(), op.bytes.begin(), op.bytes.end());
      effective_backing.SaveData(query, std::move(bytes));
    } else {
      RemoveLayerFromRam(effective_backing, query);
    }
  }
  (void)error_out;
  return true;
}

bool WriteDistilledSupplyPackageFile(
    std::filesystem::path const& path,
    DistilledSupplyPackage const& package,
    DistilledSupplyPackageError* error_out,
    bool require_canonical) {
  if (require_canonical &&
      (!IsSortedUniqueSupplyIds(package.target_supply_constant_ids) ||
       !IsSortedUniqueOps(package.operations))) {
    if (error_out != nullptr) {
      error_out->message = "package-not-canonical";
    }
    return false;
  }

  Writer writer{};
  writer.WriteU32(kDistilledSupplyPackageMagic);
  writer.WriteU32(kDistilledSupplyPackageFormatVersion);
  writer.WriteU32(package.base_release);
  writer.WriteU32(package.target_release);
  writer.WriteU32(
      static_cast<std::uint32_t>(package.target_supply_constant_ids.size()));
  for (auto id : package.target_supply_constant_ids) {
    writer.WriteU32(id);
  }
  writer.WriteU32(static_cast<std::uint32_t>(package.operations.size()));
  for (auto const& op : package.operations) {
    writer.WriteU32(op.key.object_id);
    writer.WriteU32(op.key.class_id);
    writer.WriteU8(op.key.version);
    writer.WriteU8(static_cast<std::uint8_t>(op.op));
    if (op.op == DistilledSupplyPackageOp::kUpsert) {
      writer.WriteU32(static_cast<std::uint32_t>(op.bytes.size()));
      if (!op.bytes.empty()) {
        writer.Write(op.bytes.data(), op.bytes.size());
      }
    }
  }

  std::uint32_t const crc = static_cast<std::uint32_t>(
      crc32::from_buffer(writer.data.data(), writer.data.size()));
  writer.WriteU32(crc);

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    if (error_out != nullptr) {
      error_out->message = "write-failed";
    }
    return false;
  }
  out.write(reinterpret_cast<char const*>(writer.data.data()),
            static_cast<std::streamsize>(writer.data.size()));
  return static_cast<bool>(out);
}

std::optional<std::vector<std::uint8_t>> ReadDistilledSupplyPackageFile(
    std::filesystem::path const& path,
    DistilledSupplyPackageError* error_out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    if (error_out != nullptr) {
      error_out->message = "read-failed";
    }
    return std::nullopt;
  }
  in.seekg(0, std::ios::end);
  auto const size = in.tellg();
  if (size < 0) {
    if (error_out != nullptr) {
      error_out->message = "read-failed";
    }
    return std::nullopt;
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  in.seekg(0, std::ios::beg);
  in.read(reinterpret_cast<char*>(bytes.data()),
          static_cast<std::streamsize>(bytes.size()));
  if (!in) {
    if (error_out != nullptr) {
      error_out->message = "read-failed";
    }
    return std::nullopt;
  }
  return bytes;
}

bool BuildDistilledSupplyPackageFromStorageDiff(
    ae::RamDomainStorage const& base_storage,
    ae::RamDomainStorage const& target_storage,
    std::set<ae::ObjId> const& object_filter,
    std::vector<ae::ObjId> const& target_supply_constant_ids,
    std::uint32_t base_release, std::uint32_t target_release,
    DistilledSupplyPackage& out, DistilledSupplyPackageError* error_out) {
  struct LayerEntry {
    LayerKey key;
    ae::ObjectData bytes;
    bool present{};
  };

  auto collect = [&](ae::RamDomainStorage const& storage) {
    std::vector<LayerEntry> layers;
    for (auto const& [obj_id, class_map_opt] : storage.state) {
      if (object_filter.find(obj_id) == object_filter.end() || !class_map_opt) {
        continue;
      }
      for (auto const& [class_id, version_map] : *class_map_opt) {
        for (auto const& [version, bytes] : version_map) {
          layers.push_back(
              LayerEntry{.key = {obj_id.id(), class_id, version},
                         .bytes = bytes,
                         .present = true});
        }
      }
    }
    std::sort(layers.begin(), layers.end(),
              [](LayerEntry const& a, LayerEntry const& b) {
                return LayerKeyLess(a.key, b.key);
              });
    return layers;
  };

  auto base_layers = collect(base_storage);
  auto target_layers = collect(target_storage);

  out = {};
  out.base_release = base_release;
  out.target_release = target_release;
  out.target_supply_constant_ids.reserve(target_supply_constant_ids.size());
  for (auto const& id : target_supply_constant_ids) {
    out.target_supply_constant_ids.push_back(id.id());
  }
  std::sort(out.target_supply_constant_ids.begin(),
            out.target_supply_constant_ids.end());

  std::size_t bi = 0;
  std::size_t ti = 0;
  while (bi < base_layers.size() || ti < target_layers.size()) {
    if (bi < base_layers.size() &&
        (ti >= target_layers.size() ||
         LayerKeyLess(base_layers[bi].key, target_layers[ti].key))) {
      out.operations.push_back(
          DistilledSupplyPackageOperation{
              .key = {.object_id = base_layers[bi].key.object_id,
                      .class_id = base_layers[bi].key.class_id,
                      .version = base_layers[bi].key.version},
              .op = DistilledSupplyPackageOp::kRemove});
      ++bi;
      continue;
    }
    if (ti < target_layers.size() &&
        (bi >= base_layers.size() ||
         LayerKeyLess(target_layers[ti].key, base_layers[bi].key))) {
      auto& op = out.operations.emplace_back();
      op.key = {.object_id = target_layers[ti].key.object_id,
                .class_id = target_layers[ti].key.class_id,
                .version = target_layers[ti].key.version};
      op.op = DistilledSupplyPackageOp::kUpsert;
      op.bytes.assign(target_layers[ti].bytes.begin(),
                      target_layers[ti].bytes.end());
      ++ti;
      continue;
    }
    if (base_layers[bi].bytes != target_layers[ti].bytes) {
      auto& op = out.operations.emplace_back();
      op.key = {.object_id = target_layers[ti].key.object_id,
                .class_id = target_layers[ti].key.class_id,
                .version = target_layers[ti].key.version};
      op.op = DistilledSupplyPackageOp::kUpsert;
      op.bytes.assign(target_layers[ti].bytes.begin(),
                      target_layers[ti].bytes.end());
    }
    ++bi;
    ++ti;
  }

  if (!IsSortedUniqueOps(out.operations)) {
    if (error_out != nullptr) {
      error_out->message = "diff-not-unique";
    }
    return false;
  }
  return true;
}

}  // namespace apptraverse
