#include "apptraverse/sync_packet.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <utility>

#include "aether/clock.h"

namespace apptraverse {
namespace {

void AppendU32(SerializedSyncPacket& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
}

void AppendU8(SerializedSyncPacket& out, std::uint8_t value) {
  out.push_back(value);
}

std::uint32_t ReadU32(SerializedSyncPacket const& bytes, std::size_t& offset) {
  assert(offset + 4 <= bytes.size());
  std::uint32_t value = 0;
  value |= static_cast<std::uint32_t>(bytes[offset]);
  value |= static_cast<std::uint32_t>(bytes[offset + 1]) << 8;
  value |= static_cast<std::uint32_t>(bytes[offset + 2]) << 16;
  value |= static_cast<std::uint32_t>(bytes[offset + 3]) << 24;
  offset += 4;
  return value;
}

std::uint8_t ReadU8(SerializedSyncPacket const& bytes, std::size_t& offset) {
  assert(offset < bytes.size());
  return bytes[offset++];
}

struct EncodedRecord {
  ae::ObjId obj_id;
  std::uint32_t class_id{0};
  std::uint8_t version{0};
  ae::ObjectData data;
};

SerializedSyncPacket EncodeRamStorage(ae::ObjId root_id,
                                      ae::RamDomainStorage const& storage) {
  std::vector<EncodedRecord> records;
  for (auto const& [obj_id, classes] : storage.state) {
    if (!classes.has_value()) {
      continue;
    }
    for (auto const& [class_id, versions] : *classes) {
      for (auto const& [version, data] : versions) {
        records.push_back(EncodedRecord{obj_id, class_id, version, data});
      }
    }
  }
  std::sort(records.begin(), records.end(),
            [](EncodedRecord const& a, EncodedRecord const& b) {
              if (a.obj_id != b.obj_id) {
                return a.obj_id < b.obj_id;
              }
              if (a.class_id != b.class_id) {
                return a.class_id < b.class_id;
              }
              return a.version < b.version;
            });

  SerializedSyncPacket out;
  AppendU32(out, root_id.id());
  AppendU32(out, static_cast<std::uint32_t>(records.size()));
  for (auto const& record : records) {
    AppendU32(out, record.obj_id.id());
    AppendU32(out, record.class_id);
    AppendU8(out, record.version);
    AppendU32(out, static_cast<std::uint32_t>(record.data.size()));
    out.insert(out.end(), record.data.begin(), record.data.end());
  }
  return out;
}

void ImportEncodedRecords(SerializedSyncPacket const& bytes, ae::ObjId& root_id,
                          ae::RamDomainStorage& storage) {
  std::size_t offset = 0;
  root_id = ae::ObjId{ReadU32(bytes, offset)};
  auto const count = ReadU32(bytes, offset);
  for (std::uint32_t i = 0; i < count; ++i) {
    ae::ObjId const obj_id{ReadU32(bytes, offset)};
    auto const class_id = ReadU32(bytes, offset);
    auto const version = ReadU8(bytes, offset);
    auto const data_size = ReadU32(bytes, offset);
    assert(offset + data_size <= bytes.size());
    ae::ObjectData data(
        bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        bytes.begin() + static_cast<std::ptrdiff_t>(offset + data_size));
    offset += data_size;
    storage.SaveData(ae::DomainQuery{obj_id, class_id, version},
                     std::move(data));
  }
  assert(offset == bytes.size());
}

void SaveRootToDomain(SyncPacket::ptr packet, ae::Domain& domain) {
  ae::DomainGraph graph{&domain};
  ae::Ptr<ae::Obj> const as_obj = packet.Load();
  graph.SaveRootImpl(as_obj, packet.id());
}

}  // namespace

SerializedSyncPacket SyncPacketCodec::Encode(SyncPacket::ptr packet) {
  assert(packet.is_valid());
  assert(packet.is_loaded());

  // Temp domain #1: serialize the live (already-loaded) packet graph without
  // mutating the source objects' Domain.
  ae::RamDomainStorage temp1_storage;
  ae::Domain temp1_domain{ae::Now(), temp1_storage};
  SaveRootToDomain(packet, temp1_domain);

  auto temp1_packet = SyncPacket::ptr::Declare(
      ae::CreateWith{temp1_domain}.with_id(packet.id()));
  temp1_packet.Load();
  assert(temp1_packet.is_loaded());

  // Sanitize copy: LocalPtr cleared; SharedPtr / ordinary ObjPtr loaded+kept.
  temp1_packet->PrepareSyncGraph(nullptr, SharedCopyMode::kCopyLoadedTargets);

  // Temp domain #2: save only the sanitized graph.
  ae::RamDomainStorage temp2_storage;
  ae::Domain temp2_domain{ae::Now(), temp2_storage};
  SaveRootToDomain(temp1_packet, temp2_domain);

  return EncodeRamStorage(packet.id(), temp2_storage);
}

DecodedSyncPacket SyncPacketCodec::Decode(SerializedSyncPacket const& bytes) {
  DecodedSyncPacket decoded;
  decoded.storage = std::make_unique<ae::RamDomainStorage>();
  ae::ObjId root_id;
  ImportEncodedRecords(bytes, root_id, *decoded.storage);
  decoded.domain = std::make_unique<ae::Domain>(ae::Now(), *decoded.storage);
  decoded.packet = SyncPacket::ptr::Declare(
      ae::CreateWith{*decoded.domain}.with_id(root_id));
  decoded.packet.Load();
  assert(decoded.packet.is_loaded());
  return decoded;
}

}  // namespace apptraverse
