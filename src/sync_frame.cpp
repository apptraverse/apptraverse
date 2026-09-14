#include "apptraverse/sync_frame.h"

#include <cstddef>

namespace apptraverse {
namespace {

void AppendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void AppendObjId(std::vector<std::uint8_t>& out, ae::ObjId id) {
  AppendU32(out, id.id());
}

bool ReadU32(std::vector<std::uint8_t> const& in, std::size_t& pos,
             std::uint32_t& value) {
  if (pos + 4 > in.size()) {
    return false;
  }
  value = (static_cast<std::uint32_t>(in[pos]) << 24U) |
          (static_cast<std::uint32_t>(in[pos + 1]) << 16U) |
          (static_cast<std::uint32_t>(in[pos + 2]) << 8U) |
          static_cast<std::uint32_t>(in[pos + 3]);
  pos += 4;
  return true;
}

bool ReadObjId(std::vector<std::uint8_t> const& in, std::size_t& pos,
               ae::ObjId& id) {
  std::uint32_t raw = 0;
  if (!ReadU32(in, pos, raw)) {
    return false;
  }
  id = ae::ObjId{raw};
  return true;
}

bool ReadHeader(std::vector<std::uint8_t> const& in, SyncFrameType expected,
                std::size_t& pos) {
  if (in.size() < 2) {
    return false;
  }
  if (in[0] != static_cast<std::uint8_t>(expected)) {
    return false;
  }
  if (in[1] != kSyncProtocolVersion) {
    return false;
  }
  pos = 2;
  return true;
}

void AppendHeader(std::vector<std::uint8_t>& out, SyncFrameType type) {
  out.push_back(static_cast<std::uint8_t>(type));
  out.push_back(kSyncProtocolVersion);
}

}  // namespace

bool PeekSyncFrameType(std::vector<std::uint8_t> const& bytes,
                       SyncFrameType& out) {
  if (bytes.size() < 2 || bytes[1] != kSyncProtocolVersion) {
    return false;
  }
  switch (bytes[0]) {
    case static_cast<std::uint8_t>(SyncFrameType::kNodeState):
      out = SyncFrameType::kNodeState;
      return true;
    case static_cast<std::uint8_t>(SyncFrameType::kAck):
      out = SyncFrameType::kAck;
      return true;
    default:
      return false;
  }
}

std::vector<std::uint8_t> EncodeNodeStateFrame(NodeStateFrame const& frame) {
  std::vector<std::uint8_t> out;
  AppendHeader(out, SyncFrameType::kNodeState);
  AppendObjId(out, frame.packet_id);
  AppendObjId(out, frame.target_node_id);
  AppendObjId(out, frame.destination_share_id);
  AppendU32(out, static_cast<std::uint32_t>(frame.payload.size()));
  out.insert(out.end(), frame.payload.begin(), frame.payload.end());
  return out;
}

bool DecodeNodeStateFrame(std::vector<std::uint8_t> const& bytes,
                          NodeStateFrame& out) {
  std::size_t pos = 0;
  if (!ReadHeader(bytes, SyncFrameType::kNodeState, pos)) {
    return false;
  }
  std::uint32_t payload_size = 0;
  if (!ReadObjId(bytes, pos, out.packet_id) ||
      !ReadObjId(bytes, pos, out.target_node_id) ||
      !ReadObjId(bytes, pos, out.destination_share_id) ||
      !ReadU32(bytes, pos, payload_size) ||
      pos + payload_size > bytes.size()) {
    return false;
  }
  out.payload.assign(
      bytes.begin() + static_cast<std::ptrdiff_t>(pos),
      bytes.begin() + static_cast<std::ptrdiff_t>(pos + payload_size));
  return true;
}

std::vector<std::uint8_t> EncodeAckFrame(AckFrame const& frame) {
  std::vector<std::uint8_t> out;
  AppendHeader(out, SyncFrameType::kAck);
  AppendObjId(out, frame.packet_id);
  AppendObjId(out, frame.target_node_id);
  AppendObjId(out, frame.destination_share_id);
  return out;
}

bool DecodeAckFrame(std::vector<std::uint8_t> const& bytes, AckFrame& out) {
  std::size_t pos = 0;
  if (!ReadHeader(bytes, SyncFrameType::kAck, pos)) {
    return false;
  }
  return ReadObjId(bytes, pos, out.packet_id) &&
         ReadObjId(bytes, pos, out.target_node_id) &&
         ReadObjId(bytes, pos, out.destination_share_id);
}

}  // namespace apptraverse
