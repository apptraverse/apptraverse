#ifndef APPTRAVERSE_CHAT_PRESENCE_H_
#define APPTRAVERSE_CHAT_PRESENCE_H_

#include <cstdint>
#include <string>

namespace apptraverse {

// Mirrors ae::PeerPresenceState / legacy PeerScheduleState numeric order
// (Online~Expected=0, Offline~MissedDeadline=1, Unknown=2).
inline constexpr std::uint32_t kPeerPresenceStateOnline = 0;
inline constexpr std::uint32_t kPeerPresenceStateOffline = 1;
inline constexpr std::uint32_t kPeerPresenceStateUnknown = 2;
inline constexpr std::uint32_t kPeerScheduleStateExpected = kPeerPresenceStateOnline;
inline constexpr std::uint32_t kPeerScheduleStateMissedDeadline =
    kPeerPresenceStateOffline;
inline constexpr std::uint32_t kPeerScheduleStateUnknown =
    kPeerPresenceStateUnknown;

inline bool OnlineFromPeerPresenceState(std::uint32_t state) noexcept {
  return state == kPeerPresenceStateOnline;
}

inline bool OnlineFromPeerScheduleState(std::uint32_t state) noexcept {
  return OnlineFromPeerPresenceState(state);
}

inline bool OnlineFromQuerySuccess(bool query_success,
                                   std::uint32_t schedule_state) noexcept {
  if (!query_success) {
    return false;
  }
  return OnlineFromPeerPresenceState(schedule_state);
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
