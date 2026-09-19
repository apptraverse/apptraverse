#include "apptraverse/sync_frame.h"

#include <cstddef>
#include <string>

#include "aether-objects/obj/registry.h"

#include "apptraverse/event.h"

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

void AppendU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
  AppendU32(out, static_cast<std::uint32_t>(value >> 32U));
  AppendU32(out, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
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

// Every ObjId a v1 frame carries names something the receiver must resolve,
// so the zero id is never a legal value on the wire.
bool ReadObjId(std::vector<std::uint8_t> const& in, std::size_t& pos,
               ae::ObjId& id) {
  std::uint32_t raw = 0;
  if (!ReadU32(in, pos, raw)) {
    return false;
  }
  id = ae::ObjId{raw};
  return id.is_valid();
}

bool ReadU64(std::vector<std::uint8_t> const& in, std::size_t& pos,
             std::uint64_t& value) {
  std::uint32_t hi = 0;
  std::uint32_t lo = 0;
  if (!ReadU32(in, pos, hi) || !ReadU32(in, pos, lo)) {
    return false;
  }
  value = (static_cast<std::uint64_t>(hi) << 32U) |
          static_cast<std::uint64_t>(lo);
  return true;
}

inline constexpr std::uint32_t kMaxOriginUidBytes = 256;

bool ReadBoundedBytes(std::vector<std::uint8_t> const& in, std::size_t& pos,
                      std::uint32_t max_size, std::string& out) {
  std::uint32_t size = 0;
  if (!ReadU32(in, pos, size) || size == 0 || size > max_size ||
      pos + size > in.size()) {
    return false;
  }
  out.assign(reinterpret_cast<char const*>(in.data() + pos), size);
  pos += size;
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
    case static_cast<std::uint8_t>(SyncFrameType::kEvent):
      out = SyncFrameType::kEvent;
      return true;
    case static_cast<std::uint8_t>(SyncFrameType::kShareOffer):
      out = SyncFrameType::kShareOffer;
      return true;
    case static_cast<std::uint8_t>(SyncFrameType::kShareDecision):
      out = SyncFrameType::kShareDecision;
      return true;
    case static_cast<std::uint8_t>(SyncFrameType::kShareRequest):
      out = SyncFrameType::kShareRequest;
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
      // Canonical length: the declared payload is the rest of the frame.
      // Trailing bytes mean this is not a frame we produced.
      pos + payload_size != bytes.size()) {
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
         ReadObjId(bytes, pos, out.destination_share_id) &&
         pos == bytes.size();
}

std::vector<std::uint8_t> EncodeEventFrame(EventFrame const& frame) {
  std::vector<std::uint8_t> out;
  AppendHeader(out, SyncFrameType::kEvent);
  AppendObjId(out, frame.packet_id);
  AppendObjId(out, frame.target_node_id);
  AppendObjId(out, frame.destination_share_id);
  AppendU32(out, static_cast<std::uint32_t>(frame.identity.origin_uid.size()));
  out.insert(out.end(), frame.identity.origin_uid.begin(),
             frame.identity.origin_uid.end());
  AppendU64(out, frame.identity.origin_sequence);
  AppendU64(out, frame.timestamp_us);
  AppendU32(out, frame.event_class_id);
  AppendU32(out, static_cast<std::uint32_t>(frame.payload.size()));
  out.insert(out.end(), frame.payload.begin(), frame.payload.end());
  return out;
}

bool DecodeEventFrame(std::vector<std::uint8_t> const& bytes, EventFrame& out) {
  std::size_t pos = 0;
  if (!ReadHeader(bytes, SyncFrameType::kEvent, pos)) {
    return false;
  }
  std::uint32_t payload_size = 0;
  if (!ReadObjId(bytes, pos, out.packet_id) ||
      !ReadObjId(bytes, pos, out.target_node_id) ||
      !ReadObjId(bytes, pos, out.destination_share_id) ||
      !ReadBoundedBytes(bytes, pos, kMaxOriginUidBytes,
                        out.identity.origin_uid) ||
      !ReadU64(bytes, pos, out.identity.origin_sequence) ||
      out.identity.origin_sequence == 0 ||
      !ReadU64(bytes, pos, out.timestamp_us) || out.timestamp_us == 0 ||
      !ReadU32(bytes, pos, out.event_class_id) || out.event_class_id == 0 ||
      ae::Registry::GetRegistry().GenerationDistance(Event::kClassId,
                                                     out.event_class_id) < 0 ||
      !ReadU32(bytes, pos, payload_size) ||
      pos + payload_size != bytes.size()) {
    return false;
  }
  out.payload.assign(
      bytes.begin() + static_cast<std::ptrdiff_t>(pos),
      bytes.begin() + static_cast<std::ptrdiff_t>(pos + payload_size));
  return true;
}

namespace {

bool ReadAccess(std::vector<std::uint8_t> const& in, std::size_t& pos,
                std::uint8_t& access) {
  if (pos >= in.size()) {
    return false;
  }
  access = in[pos];
  ++pos;
  // Only the two ShareAccess values are legal. Anything else is a bad frame.
  return access <= 1;
}

}  // namespace

std::vector<std::uint8_t> EncodeShareOfferFrame(ShareOfferFrame const& frame) {
  std::vector<std::uint8_t> out;
  AppendHeader(out, SyncFrameType::kShareOffer);
  AppendObjId(out, frame.packet_id);
  AppendObjId(out, frame.operation_id);
  AppendObjId(out, frame.target_node_id);
  AppendU32(out, frame.root_class_id);
  out.push_back(frame.access);
  return out;
}

bool DecodeShareOfferFrame(std::vector<std::uint8_t> const& bytes,
                           ShareOfferFrame& out) {
  std::size_t pos = 0;
  if (!ReadHeader(bytes, SyncFrameType::kShareOffer, pos)) {
    return false;
  }
  return ReadObjId(bytes, pos, out.packet_id) &&
         ReadObjId(bytes, pos, out.operation_id) &&
         ReadObjId(bytes, pos, out.target_node_id) &&
         ReadU32(bytes, pos, out.root_class_id) && out.root_class_id != 0 &&
         ReadAccess(bytes, pos, out.access) && pos == bytes.size();
}

std::vector<std::uint8_t> EncodeShareRequestFrame(ShareOfferFrame const& frame) {
  std::vector<std::uint8_t> out;
  AppendHeader(out, SyncFrameType::kShareRequest);
  AppendObjId(out, frame.packet_id);
  AppendObjId(out, frame.operation_id);
  AppendObjId(out, frame.target_node_id);
  AppendU32(out, frame.root_class_id);
  out.push_back(frame.access);
  return out;
}

bool DecodeShareRequestFrame(std::vector<std::uint8_t> const& bytes,
                             ShareOfferFrame& out) {
  std::size_t pos = 0;
  if (!ReadHeader(bytes, SyncFrameType::kShareRequest, pos)) {
    return false;
  }
  return ReadObjId(bytes, pos, out.packet_id) &&
         ReadObjId(bytes, pos, out.operation_id) &&
         ReadObjId(bytes, pos, out.target_node_id) &&
         ReadU32(bytes, pos, out.root_class_id) &&
         ReadAccess(bytes, pos, out.access) && pos == bytes.size();
}

std::vector<std::uint8_t> EncodeShareDecisionFrame(
    ShareDecisionFrame const& frame) {
  std::vector<std::uint8_t> out;
  AppendHeader(out, SyncFrameType::kShareDecision);
  AppendObjId(out, frame.packet_id);
  AppendObjId(out, frame.operation_id);
  AppendObjId(out, frame.target_node_id);
  AppendU32(out, frame.root_class_id);
  out.push_back(frame.access);
  out.push_back(frame.accepted ? std::uint8_t{1} : std::uint8_t{0});
  return out;
}

bool DecodeShareDecisionFrame(std::vector<std::uint8_t> const& bytes,
                              ShareDecisionFrame& out) {
  std::size_t pos = 0;
  if (!ReadHeader(bytes, SyncFrameType::kShareDecision, pos)) {
    return false;
  }
  std::uint8_t accepted = 0;
  if (!ReadObjId(bytes, pos, out.packet_id) ||
      !ReadObjId(bytes, pos, out.operation_id) ||
      !ReadObjId(bytes, pos, out.target_node_id) ||
      !ReadU32(bytes, pos, out.root_class_id) || out.root_class_id == 0 ||
      !ReadAccess(bytes, pos, out.access) || pos >= bytes.size()) {
    return false;
  }
  accepted = bytes[pos];
  ++pos;
  if ((accepted != 0 && accepted != 1) || pos != bytes.size()) {
    return false;
  }
  out.accepted = accepted == 1;
  return true;
}

}  // namespace apptraverse
