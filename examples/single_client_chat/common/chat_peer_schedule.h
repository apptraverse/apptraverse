#ifndef APPTRAVERSE_CHAT_PEER_SCHEDULE_H_
#define APPTRAVERSE_CHAT_PEER_SCHEDULE_H_

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

#include "aether/clock.h"
#include "aether/receive_schedule.h"

namespace ae {
struct Uid;
}

namespace apptraverse::chat {

inline constexpr auto kPresenceRefreshInterval = std::chrono::seconds{3};
// Interactive Windows chat: Aether receive cycle (not presence query interval).
inline constexpr auto kReceivePingInterval = std::chrono::seconds{1};
inline constexpr auto kReceiveWindow = std::chrono::seconds{1};
inline constexpr auto kPeerScheduleGrace = std::chrono::milliseconds{500};

enum class PeerPresenceStatus : std::uint8_t {
  kUnknown = 0,
  kOnline = 1,
  kOffline = 2,
  kNotRunning = 3,
};

enum class LocalPresenceStatus : std::uint8_t {
  kConnecting = 0,
  kOnline = 1,
  kOffline = 2,
};

// Thin wrapper over ae::PeerReceiveSchedule fields for app-layer use.
struct PeerScheduleSnapshot {
  ae::TimePoint last_online{};
  std::optional<ae::TimePoint> next_ping_deadline;
  ae::PeerScheduleState schedule_state{ae::PeerScheduleState::kUnknown};
};

using PeerScheduleQueryCallback =
    std::function<void(std::optional<PeerScheduleSnapshot>)>;
using QueryPeerScheduleFunction =
    std::function<void(ae::Uid const& peer, PeerScheduleQueryCallback)>;

inline PeerScheduleSnapshot MakePeerScheduleSnapshot(
    ae::PeerReceiveSchedule const& in) {
  PeerScheduleSnapshot out{};
  out.last_online = in.last_online;
  out.next_ping_deadline = in.next_ping_deadline;
  out.schedule_state = in.state;
  return out;
}

// Successful schedule with kUnknown and no next_ping_deadline => NotRunning.
// Query failure is handled by the caller (leave/set Unknown).
inline PeerPresenceStatus ClassifyPeerPresence(
    PeerScheduleSnapshot const& snap) noexcept {
  switch (snap.schedule_state) {
    case ae::PeerScheduleState::kExpected:
      if (snap.next_ping_deadline.has_value()) {
        return PeerPresenceStatus::kOnline;
      }
      return PeerPresenceStatus::kUnknown;
    case ae::PeerScheduleState::kMissedDeadline:
      return PeerPresenceStatus::kOffline;
    case ae::PeerScheduleState::kUnknown:
      if (!snap.next_ping_deadline.has_value()) {
        return PeerPresenceStatus::kNotRunning;
      }
      return PeerPresenceStatus::kUnknown;
  }
  return PeerPresenceStatus::kUnknown;
}

inline LocalPresenceStatus ClassifyLocalPresence(
    bool ever_succeeded,
    std::optional<PeerScheduleSnapshot> const& snap) noexcept {
  if (!ever_succeeded || !snap.has_value()) {
    return ever_succeeded ? LocalPresenceStatus::kOffline
                          : LocalPresenceStatus::kConnecting;
  }
  switch (snap->schedule_state) {
    case ae::PeerScheduleState::kExpected:
      return LocalPresenceStatus::kOnline;
    case ae::PeerScheduleState::kMissedDeadline:
    case ae::PeerScheduleState::kUnknown:
      return LocalPresenceStatus::kOffline;
  }
  return LocalPresenceStatus::kConnecting;
}

inline char const* PeerPresenceStatusName(PeerPresenceStatus s) noexcept {
  switch (s) {
    case PeerPresenceStatus::kOnline:
      return "Online";
    case PeerPresenceStatus::kOffline:
      return "Offline";
    case PeerPresenceStatus::kNotRunning:
      return "Not running";
    case PeerPresenceStatus::kUnknown:
      return "Unknown";
  }
  return "Unknown";
}

inline char const* LocalPresenceStatusName(LocalPresenceStatus s) noexcept {
  switch (s) {
    case LocalPresenceStatus::kConnecting:
      return "Connecting";
    case LocalPresenceStatus::kOnline:
      return "Online";
    case LocalPresenceStatus::kOffline:
      return "Offline";
  }
  return "Connecting";
}

inline char const* PeerScheduleStateName(ae::PeerScheduleState s) noexcept {
  switch (s) {
    case ae::PeerScheduleState::kExpected:
      return "Expected";
    case ae::PeerScheduleState::kMissedDeadline:
      return "MissedDeadline";
    case ae::PeerScheduleState::kUnknown:
      return "Unknown";
  }
  return "Unknown";
}

}  // namespace apptraverse::chat

#endif  // APPTRAVERSE_CHAT_PEER_SCHEDULE_H_
