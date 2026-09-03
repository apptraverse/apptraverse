#ifndef APPTRAVERSE_CHAT_PRESENCE_H_
#define APPTRAVERSE_CHAT_PRESENCE_H_

#include <cstdint>
#include <string>
#include <string_view>

namespace apptraverse {

// Platform-neutral Presence for chat contacts. Authoritative runtime value is
// produced on the Aether thread and applied via ChatPresenceOverlay.
enum class PresenceState : std::uint8_t {
  kUnknown = 0,
  kOnline = 1,
  kOffline = 2,
};

inline char const* PresenceStateName(PresenceState state) noexcept {
  switch (state) {
    case PresenceState::kOnline:
      return "online";
    case PresenceState::kOffline:
      return "offline";
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

// Remote PeerPresenceState mapping by named cases only — callers pass the
// already-discriminated Aether enumerator; do not rely on numeric values.
enum class PeerPresenceCase : std::uint8_t {
  kOnline,
  kOffline,
  kUnknown,
  kQueryError,
};

inline PresenceState PresenceFromPeerCase(PeerPresenceCase value) noexcept {
  switch (value) {
    case PeerPresenceCase::kOnline:
      return PresenceState::kOnline;
    case PeerPresenceCase::kOffline:
      return PresenceState::kOffline;
    case PeerPresenceCase::kUnknown:
    case PeerPresenceCase::kQueryError:
      return PresenceState::kUnknown;
  }
  return PresenceState::kUnknown;
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
    case PresenceState::kUnknown:
      return L"? ";
  }
  return L"? ";
}

inline std::wstring FormatContactPresenceLabel(PresenceState state,
                                               std::wstring const& name) {
  return ContactPresencePrefix(state) + name;
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_PRESENCE_H_
