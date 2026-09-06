#ifndef CHAT_PRESENCE_H_
#define CHAT_PRESENCE_H_

#include <cstdint>
#include <string>
#include <string_view>

namespace chat {

// Local connectivity Presence is diagnosed on the Aether thread and committed
// to ChatClient as PresenceMonitoringStartedEvent / PresenceChangedEvent on
// the Model thread.
enum class PresenceState : std::uint8_t {
  kUnknown = 0,
  kOnline = 1,
  kOffline = 2,
  kConnecting = 3,
};

inline char const* PresenceStateName(PresenceState state) noexcept {
  switch (state) {
    case PresenceState::kOnline:
      return "online";
    case PresenceState::kOffline:
      return "offline";
    case PresenceState::kConnecting:
      return "connecting";
    case PresenceState::kUnknown:
      return "unknown";
  }
  return "unknown";
}

inline PresenceState PresenceStateFromName(std::string_view name) noexcept {
  if (name == "online") {
    return PresenceState::kOnline;
  }
  if (name == "offline") {
    return PresenceState::kOffline;
  }
  if (name == "connecting") {
    return PresenceState::kConnecting;
  }
  return PresenceState::kUnknown;
}

// Local connectivity diagnostic classification (has_schedule / any_online).
inline PresenceState PresenceFromLocalDiag(bool has_schedule,
                                           bool any_online) noexcept {
  if (!has_schedule) {
    return PresenceState::kUnknown;
  }
  if (any_online) {
    return PresenceState::kOnline;
  }
  return PresenceState::kOffline;
}

inline bool PresenceIsOnline(PresenceState state) noexcept {
  return state == PresenceState::kOnline;
}

inline std::wstring ContactPresencePrefix(PresenceState state) {
  switch (state) {
    case PresenceState::kOnline:
      return L"\u25CF ";
    case PresenceState::kOffline:
      return L"\u25CB ";
    case PresenceState::kConnecting:
      return L"\u25CC ";
    case PresenceState::kUnknown:
      return L"? ";
  }
  return L"? ";
}

inline std::wstring FormatContactPresenceLabel(PresenceState state,
                                               std::wstring const& name) {
  return ContactPresencePrefix(state) + name;
}

}  // namespace chat

#endif  // CHAT_PRESENCE_H_
