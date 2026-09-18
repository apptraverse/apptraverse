#ifndef APPTRAVERSE_MESSENGER_DIAL_H_
#define APPTRAVERSE_MESSENGER_DIAL_H_

#include <cstdint>
#include <cstring>
#include <vector>

namespace apptraverse::messenger_dial {

// Control-plane only. Chat text and journal bytes stay on SharedSyncRuntime.
inline constexpr char kMagic[4] = {'M', 'S', 'G', 'D'};

enum class Kind : std::uint8_t {
  DialRequest = 1,
  DialAck = 2,
};

inline bool Encode(Kind kind, std::vector<std::uint8_t>& out) {
  out.clear();
  out.reserve(5);
  out.push_back(static_cast<std::uint8_t>(kMagic[0]));
  out.push_back(static_cast<std::uint8_t>(kMagic[1]));
  out.push_back(static_cast<std::uint8_t>(kMagic[2]));
  out.push_back(static_cast<std::uint8_t>(kMagic[3]));
  out.push_back(static_cast<std::uint8_t>(kind));
  return true;
}

inline bool Decode(std::vector<std::uint8_t> const& bytes, Kind& kind) {
  if (bytes.size() != 5) {
    return false;
  }
  if (bytes[0] != static_cast<std::uint8_t>(kMagic[0]) ||
      bytes[1] != static_cast<std::uint8_t>(kMagic[1]) ||
      bytes[2] != static_cast<std::uint8_t>(kMagic[2]) ||
      bytes[3] != static_cast<std::uint8_t>(kMagic[3])) {
    return false;
  }
  auto const raw = bytes[4];
  if (raw != static_cast<std::uint8_t>(Kind::DialRequest) &&
      raw != static_cast<std::uint8_t>(Kind::DialAck)) {
    return false;
  }
  kind = static_cast<Kind>(raw);
  return true;
}

}  // namespace apptraverse::messenger_dial

#endif  // APPTRAVERSE_MESSENGER_DIAL_H_
