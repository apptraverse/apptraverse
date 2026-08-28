#include "apptraverse/shared_frame_codec.h"

#include <cstring>
#include <string>

namespace apptraverse {
namespace {

void AppendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void AppendU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (int shift = 56; shift >= 0; shift -= 8) {
    out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
  }
}

void AppendString(std::vector<std::uint8_t>& out, std::string const& value) {
  AppendU32(out, static_cast<std::uint32_t>(value.size()));
  out.insert(out.end(), value.begin(), value.end());
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

bool ReadU64(std::vector<std::uint8_t> const& in, std::size_t& pos,
             std::uint64_t& value) {
  if (pos + 8 > in.size()) {
    return false;
  }
  value = 0;
  for (int i = 0; i < 8; ++i) {
    value = (value << 8U) | static_cast<std::uint64_t>(in[pos + i]);
  }
  pos += 8;
  return true;
}

bool ReadString(std::vector<std::uint8_t> const& in, std::size_t& pos,
                std::string& value) {
  std::uint32_t size = 0;
  if (!ReadU32(in, pos, size)) {
    return false;
  }
  if (pos + size > in.size()) {
    return false;
  }
  value.assign(reinterpret_cast<char const*>(in.data() + pos), size);
  pos += size;
  return true;
}

void AppendEventId(std::vector<std::uint8_t>& out, SharedEventId const& id) {
  AppendString(out, id.origin_uid);
  AppendU64(out, id.origin_sequence);
}

bool ReadEventId(std::vector<std::uint8_t> const& in, std::size_t& pos,
                 SharedEventId& id) {
  return ReadString(in, pos, id.origin_uid) && ReadU64(in, pos, id.origin_sequence);
}

void AppendOrder(std::vector<std::uint8_t>& out, SharedEventOrder const& order) {
  AppendU64(out, order.lamport);
  AppendString(out, order.origin_uid);
  AppendU64(out, order.origin_sequence);
}

bool ReadOrder(std::vector<std::uint8_t> const& in, std::size_t& pos,
               SharedEventOrder& order) {
  return ReadU64(in, pos, order.lamport) &&
         ReadString(in, pos, order.origin_uid) &&
         ReadU64(in, pos, order.origin_sequence);
}

}  // namespace

std::vector<std::uint8_t> EncodeSharedEventFrame(SharedEventFrame const& frame) {
  std::vector<std::uint8_t> out;
  out.push_back(static_cast<std::uint8_t>(SharedFrameType::kEvent));
  AppendString(out, frame.shared_room_id);
  AppendEventId(out, frame.event_id);
  AppendOrder(out, frame.order);
  AppendU32(out, static_cast<std::uint32_t>(frame.payload.size()));
  out.insert(out.end(), frame.payload.begin(), frame.payload.end());
  return out;
}

bool DecodeSharedEventFrame(std::vector<std::uint8_t> const& bytes,
                            SharedEventFrame& out) {
  if (bytes.empty() ||
      bytes[0] != static_cast<std::uint8_t>(SharedFrameType::kEvent)) {
    return false;
  }
  std::size_t pos = 1;
  std::uint32_t payload_size = 0;
  return ReadString(bytes, pos, out.shared_room_id) &&
         ReadEventId(bytes, pos, out.event_id) &&
         ReadOrder(bytes, pos, out.order) && ReadU32(bytes, pos, payload_size) &&
         pos + payload_size <= bytes.size() &&
         (out.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(pos),
                             bytes.begin() +
                                 static_cast<std::ptrdiff_t>(pos + payload_size)),
          true);
}

std::vector<std::uint8_t> EncodeSharedAckFrame(SharedAckFrame const& frame) {
  std::vector<std::uint8_t> out;
  out.push_back(static_cast<std::uint8_t>(SharedFrameType::kAck));
  AppendString(out, frame.shared_room_id);
  AppendEventId(out, frame.event_id);
  return out;
}

bool DecodeSharedAckFrame(std::vector<std::uint8_t> const& bytes,
                          SharedAckFrame& out) {
  if (bytes.empty() ||
      bytes[0] != static_cast<std::uint8_t>(SharedFrameType::kAck)) {
    return false;
  }
  std::size_t pos = 1;
  return ReadString(bytes, pos, out.shared_room_id) &&
         ReadEventId(bytes, pos, out.event_id);
}

}  // namespace apptraverse
