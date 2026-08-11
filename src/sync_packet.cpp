#include "apptraverse/sync_packet.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include "apptraverse/object_state_transfer.h"

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

ObjectState ExtractPacketObjectState(SyncPacket::ptr packet,
                                     ae::RamDomainStorage const& storage) {
  assert(packet.is_valid());
  assert(packet.is_loaded());
  packet.Save();

  ObjectState state;
  state.root_id = packet.id();
  auto const it = storage.state.find(packet.id());
  assert(it != storage.state.end());
  assert(it->second.has_value());
  for (auto const& [class_id, versions] : *it->second) {
    for (auto const& [version, data] : versions) {
      state.objects.push_back(StoredObjectVersion{
          packet.id(),
          class_id,
          version,
          data,
      });
    }
  }
  std::sort(state.objects.begin(), state.objects.end(),
            [](StoredObjectVersion const& a, StoredObjectVersion const& b) {
              if (a.obj_id != b.obj_id) {
                return a.obj_id < b.obj_id;
              }
              if (a.class_id != b.class_id) {
                return a.class_id < b.class_id;
              }
              return a.version < b.version;
            });
  return state;
}

}  // namespace

SyncPacketCodec::SyncPacketCodec() : domain_{ae::Now(), storage_} {}

SerializedSyncPacket EncodeObjectState(ObjectState const& state) {
  SerializedSyncPacket out;
  AppendU32(out, state.root_id.id());
  AppendU32(out, static_cast<std::uint32_t>(state.objects.size()));
  for (auto const& object : state.objects) {
    AppendU32(out, object.obj_id.id());
    AppendU32(out, object.class_id);
    AppendU8(out, object.version);
    AppendU32(out, static_cast<std::uint32_t>(object.data.size()));
    out.insert(out.end(), object.data.begin(), object.data.end());
  }
  return out;
}

ObjectState DecodeObjectState(SerializedSyncPacket const& bytes) {
  std::size_t offset = 0;
  ObjectState state;
  state.root_id = ae::ObjId{ReadU32(bytes, offset)};
  auto const count = ReadU32(bytes, offset);
  state.objects.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    StoredObjectVersion object;
    object.obj_id = ae::ObjId{ReadU32(bytes, offset)};
    object.class_id = ReadU32(bytes, offset);
    object.version = ReadU8(bytes, offset);
    auto const data_size = ReadU32(bytes, offset);
    assert(offset + data_size <= bytes.size());
    object.data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                       bytes.begin() + static_cast<std::ptrdiff_t>(offset +
                                                                  data_size));
    offset += data_size;
    state.objects.push_back(std::move(object));
  }
  assert(offset == bytes.size());
  return state;
}

SerializedSyncPacket SyncPacketCodec::Encode(SyncPacket::ptr packet) {
  assert(packet.is_valid());
  assert(packet.is_loaded());
  assert(packet.domain() == &domain_);
  auto const state = ExtractPacketObjectState(packet, storage_);
  return EncodeObjectState(state);
}

SyncPacket::ptr SyncPacketCodec::Decode(SerializedSyncPacket const& bytes) {
  auto const state = DecodeObjectState(bytes);
  ImportObjectState(state, storage_);
  auto packet =
      SyncPacket::ptr::Declare(ae::CreateWith{domain_}.with_id(state.root_id));
  packet.Load();
  assert(packet.is_loaded());
  assert(packet.domain() == &domain_);
  return packet;
}

std::vector<ae::ObjId> DiscoverSharedDependencies(Node::ptr node) {
  assert(node.is_valid());
  assert(node.is_loaded());
  detail::SharedDependencyCollector deps;
  node->CollectSharedDependencies(deps);
  deps.Sort();
  return deps.ids;
}

std::vector<ae::ObjId> DiscoverSharedDependencies(Event::ptr event) {
  assert(event.is_valid());
  assert(event.is_loaded());
  detail::SharedDependencyCollector deps;
  event->CollectSharedDependencies(deps);
  deps.Sort();
  return deps.ids;
}

}  // namespace apptraverse
