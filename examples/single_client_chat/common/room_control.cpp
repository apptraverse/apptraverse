#include "room_control.h"

#include <array>
#include <cstring>

namespace apptraverse::chat {
namespace {

constexpr char kMagic[4] = {'A', 'T', 'R', 'M'};

void AppendU8(std::vector<std::uint8_t>& out, std::uint8_t v) { out.push_back(v); }

void AppendU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xff));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
  out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
  out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xff));
}

void AppendU64(std::vector<std::uint8_t>& out, std::uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
  }
}

void AppendBytes(std::vector<std::uint8_t>& out, std::uint8_t const* p,
                 std::size_t n) {
  out.insert(out.end(), p, p + n);
}

bool ReadU8(std::uint8_t const*& p, std::uint8_t const* end, std::uint8_t& v) {
  if (p >= end) {
    return false;
  }
  v = *p++;
  return true;
}

bool ReadU32(std::uint8_t const*& p, std::uint8_t const* end, std::uint32_t& v) {
  if (static_cast<std::size_t>(end - p) < 4) {
    return false;
  }
  v = static_cast<std::uint32_t>(p[0]) |
      (static_cast<std::uint32_t>(p[1]) << 8) |
      (static_cast<std::uint32_t>(p[2]) << 16) |
      (static_cast<std::uint32_t>(p[3]) << 24);
  p += 4;
  return true;
}

bool ReadU64(std::uint8_t const*& p, std::uint8_t const* end, std::uint64_t& v) {
  if (static_cast<std::size_t>(end - p) < 8) {
    return false;
  }
  v = 0;
  for (int i = 0; i < 8; ++i) {
    v |= (static_cast<std::uint64_t>(p[i]) << (8 * i));
  }
  p += 8;
  return true;
}

bool ReadExact(std::uint8_t const*& p, std::uint8_t const* end, std::uint8_t* dst,
               std::size_t n) {
  if (static_cast<std::size_t>(end - p) < n) {
    return false;
  }
  std::memcpy(dst, p, n);
  p += n;
  return true;
}

bool EncodeParticipant(std::vector<std::uint8_t>& out,
                       RoomParticipantDesc const& part) {
  if (part.display_name.size() > kRoomControlMaxNameBytes) {
    return false;
  }
  AppendBytes(out, part.uid.value.data(), ae::Uid::kSize);
  AppendU32(out, part.client_obj_id);
  AppendU8(out, static_cast<std::uint8_t>(part.display_name.size()));
  AppendBytes(out,
              reinterpret_cast<std::uint8_t const*>(part.display_name.data()),
              part.display_name.size());
  return true;
}

bool DecodeParticipant(std::uint8_t const*& p, std::uint8_t const* end,
                       RoomParticipantDesc& part) {
  std::array<std::uint8_t, ae::Uid::kSize> uid_bytes{};
  if (!ReadExact(p, end, uid_bytes.data(), ae::Uid::kSize)) {
    return false;
  }
  part.uid = ae::Uid{uid_bytes};
  if (!ReadU32(p, end, part.client_obj_id)) {
    return false;
  }
  std::uint8_t name_len = 0;
  if (!ReadU8(p, end, name_len) || name_len > kRoomControlMaxNameBytes) {
    return false;
  }
  if (static_cast<std::size_t>(end - p) < name_len) {
    return false;
  }
  part.display_name.assign(reinterpret_cast<char const*>(p),
                           reinterpret_cast<char const*>(p) + name_len);
  p += name_len;
  return true;
}

bool NeedsParticipantList(RoomControlType type) {
  return type == RoomControlType::kJoinRoomAccepted ||
         type == RoomControlType::kParticipantsChanged;
}

bool IsKnownType(RoomControlType type) {
  switch (type) {
    case RoomControlType::kJoinRoomRequest:
    case RoomControlType::kJoinRoomAccepted:
    case RoomControlType::kJoinRoomRejected:
    case RoomControlType::kParticipantsChanged:
    case RoomControlType::kJoinRoomAcceptedAck:
      return true;
  }
  return false;
}

}  // namespace

bool IsRoomControlPayload(std::uint8_t const* data, std::size_t size) {
  return size >= 4 && std::memcmp(data, kMagic, 4) == 0;
}

std::vector<std::uint8_t> EncodeRoomControl(RoomControlMessage const& msg) {
  if (!IsKnownType(msg.type)) {
    return {};
  }
  std::vector<std::uint8_t> out;
  out.reserve(64);
  AppendBytes(out, reinterpret_cast<std::uint8_t const*>(kMagic), 4);
  AppendU8(out, kRoomControlVersion);
  AppendU8(out, static_cast<std::uint8_t>(msg.type));
  AppendU64(out, msg.revision);
  AppendU64(out, msg.request_id);

  if (msg.type == RoomControlType::kJoinRoomRequest) {
    if (msg.display_name.empty() ||
        msg.display_name.size() > kRoomControlMaxNameBytes) {
      return {};
    }
    AppendU32(out, msg.client_obj_id);
    AppendU8(out, static_cast<std::uint8_t>(msg.display_name.size()));
    AppendBytes(
        out, reinterpret_cast<std::uint8_t const*>(msg.display_name.data()),
        msg.display_name.size());
    return out;
  }

  if (msg.type == RoomControlType::kJoinRoomRejected) {
    if (msg.display_name.size() > kRoomControlMaxNameBytes) {
      return {};
    }
    AppendU8(out, static_cast<std::uint8_t>(msg.display_name.size()));
    AppendBytes(
        out, reinterpret_cast<std::uint8_t const*>(msg.display_name.data()),
        msg.display_name.size());
    return out;
  }

  if (NeedsParticipantList(msg.type)) {
    if (msg.participants.size() > kRoomControlMaxParticipants) {
      return {};
    }
    AppendU8(out, static_cast<std::uint8_t>(msg.participants.size()));
    for (auto const& part : msg.participants) {
      if (!EncodeParticipant(out, part)) {
        return {};
      }
    }
    return out;
  }

  // JoinRoomAcceptedAck: revision + request_id only.
  return out;
}

std::optional<RoomControlMessage> TryDecodeRoomControl(std::uint8_t const* data,
                                                       std::size_t size) {
  // Header: magic(4) + version(1) + type(1) + revision(8) + request_id(8)
  if (!IsRoomControlPayload(data, size) || size < 4 + 1 + 1 + 8 + 8) {
    return std::nullopt;
  }
  auto const* p = data + 4;
  auto const* end = data + size;
  std::uint8_t version = 0;
  std::uint8_t type_raw = 0;
  if (!ReadU8(p, end, version) || version != kRoomControlVersion) {
    return std::nullopt;
  }
  if (!ReadU8(p, end, type_raw)) {
    return std::nullopt;
  }
  RoomControlMessage msg{};
  msg.type = static_cast<RoomControlType>(type_raw);
  if (!IsKnownType(msg.type)) {
    return std::nullopt;
  }
  if (!ReadU64(p, end, msg.revision) || !ReadU64(p, end, msg.request_id)) {
    return std::nullopt;
  }

  if (msg.type == RoomControlType::kJoinRoomRequest) {
    if (!ReadU32(p, end, msg.client_obj_id)) {
      return std::nullopt;
    }
    std::uint8_t name_len = 0;
    if (!ReadU8(p, end, name_len) || name_len == 0 ||
        name_len > kRoomControlMaxNameBytes) {
      return std::nullopt;
    }
    if (static_cast<std::size_t>(end - p) != name_len) {
      return std::nullopt;
    }
    msg.display_name.assign(reinterpret_cast<char const*>(p),
                            reinterpret_cast<char const*>(p) + name_len);
    return msg;
  }

  if (msg.type == RoomControlType::kJoinRoomRejected) {
    std::uint8_t reason_len = 0;
    if (!ReadU8(p, end, reason_len) ||
        reason_len > kRoomControlMaxNameBytes) {
      return std::nullopt;
    }
    if (static_cast<std::size_t>(end - p) != reason_len) {
      return std::nullopt;
    }
    msg.display_name.assign(reinterpret_cast<char const*>(p),
                            reinterpret_cast<char const*>(p) + reason_len);
    return msg;
  }

  if (NeedsParticipantList(msg.type)) {
    std::uint8_t count = 0;
    if (!ReadU8(p, end, count) || count > kRoomControlMaxParticipants) {
      return std::nullopt;
    }
    msg.participants.resize(count);
    for (std::uint8_t i = 0; i < count; ++i) {
      if (!DecodeParticipant(p, end, msg.participants[i])) {
        return std::nullopt;
      }
    }
    if (p != end) {
      return std::nullopt;
    }
    return msg;
  }

  // JoinRoomAcceptedAck
  if (p != end) {
    return std::nullopt;
  }
  return msg;
}

}  // namespace apptraverse::chat
