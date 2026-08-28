#ifndef APPTRAVERSE_CHAT_PRESENCE_H_
#define APPTRAVERSE_CHAT_PRESENCE_H_

#include <cstdint>
#include <string>

namespace apptraverse {

// Mirrors ae::PeerScheduleState numeric order for unit tests without cloud.
inline constexpr std::uint32_t kPeerScheduleStateExpected = 0;
inline constexpr std::uint32_t kPeerScheduleStateMissedDeadline = 1;
inline constexpr std::uint32_t kPeerScheduleStateUnknown = 2;

inline bool OnlineFromPeerScheduleState(std::uint32_t state) noexcept {
  return state == kPeerScheduleStateExpected;
}

inline bool OnlineFromQuerySuccess(bool query_success,
                                   std::uint32_t schedule_state) noexcept {
  if (!query_success) {
    return false;
  }
  return OnlineFromPeerScheduleState(schedule_state);
}

inline std::wstring ContactPresencePrefix(bool online) {
  return online ? L"\u25CF " : L"\u25CB ";
}

inline std::wstring FormatContactPresenceLabel(bool online,
                                             std::wstring const& name) {
  return ContactPresencePrefix(online) + name;
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_PRESENCE_H_
