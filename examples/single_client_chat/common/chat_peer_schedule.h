#ifndef APPTRAVERSE_CHAT_PEER_SCHEDULE_H_
#define APPTRAVERSE_CHAT_PEER_SCHEDULE_H_

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

#include "aether/clock.h"
#include "aether/receive_schedule.h"

namespace ae {
struct Uid;
}

namespace apptraverse::chat {

inline constexpr std::string_view kOfflinePingMarker{"\xE2\x8F\xB1"};
inline constexpr std::chrono::milliseconds kPeerScheduleGrace{500};
inline constexpr std::chrono::milliseconds kPresenceRefreshInterval{1000};

// Display / control-plane peer presence derived from ae::PeerReceiveSchedule.
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

// Adapter snapshot over ae::PeerReceiveSchedule (library TimePoint timeline).
struct PeerScheduleSnapshot {
  ae::TimePoint last_online{};
  std::optional<ae::TimePoint> next_ping_deadline;
  ae::PeerScheduleState schedule_state{ae::PeerScheduleState::kUnknown};
  // next_ping_deadline + grace; used for retry opportunity planning.
  std::optional<ae::TimePoint> retry_after;
};

using PeerScheduleQueryCallback =
    std::function<void(std::optional<PeerScheduleSnapshot>)>;
using QueryPeerScheduleFunction =
    std::function<void(ae::Uid const& peer, PeerScheduleQueryCallback)>;

inline bool ScheduleQueryFailed(
    std::optional<PeerScheduleSnapshot> const& r) {
  return !r.has_value();
}

inline PeerScheduleSnapshot MakePeerScheduleSnapshot(
    ae::PeerReceiveSchedule const& in,
    ae::Duration grace = kPeerScheduleGrace) {
  PeerScheduleSnapshot out{};
  out.last_online = in.last_online;
  out.next_ping_deadline = in.next_ping_deadline;
  out.schedule_state = in.state;
  if (in.next_ping_deadline.has_value()) {
    out.retry_after = *in.next_ping_deadline + grace;
  }
  return out;
}

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
      // Successful API sample with no announced next window.
      if (!snap.next_ping_deadline.has_value()) {
        return PeerPresenceStatus::kNotRunning;
      }
      return PeerPresenceStatus::kUnknown;
  }
  return PeerPresenceStatus::kUnknown;
}

inline LocalPresenceStatus ClassifyLocalPresence(
    bool ever_succeeded, std::optional<PeerScheduleSnapshot> const& snap) {
  if (!ever_succeeded || !snap.has_value()) {
    return ever_succeeded ? LocalPresenceStatus::kOffline
                          : LocalPresenceStatus::kConnecting;
  }
  switch (snap->schedule_state) {
    case ae::PeerScheduleState::kExpected:
      return LocalPresenceStatus::kOnline;
    case ae::PeerScheduleState::kMissedDeadline:
      return LocalPresenceStatus::kOffline;
    case ae::PeerScheduleState::kUnknown:
      // Self with no next window: treat as offline for local UI.
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

// Retries for pending payloads: only while peer is Online.
inline bool PayloadRetriesAllowed(PeerPresenceStatus status) noexcept {
  return status == PeerPresenceStatus::kOnline;
}

// Initial user-message send may proceed when Online, or once while Unknown if
// the transport path is already usable. Never while Offline / Not running.
inline bool InitialSendAllowed(PeerPresenceStatus status) noexcept {
  return status == PeerPresenceStatus::kOnline ||
         status == PeerPresenceStatus::kUnknown;
}

inline bool ConfirmedOfflineHold(PeerPresenceStatus status) noexcept {
  return status == PeerPresenceStatus::kOffline ||
         status == PeerPresenceStatus::kNotRunning;
}

inline bool ShowOfflinePingMarker(PeerPresenceStatus status) noexcept {
  return ConfirmedOfflineHold(status);
}

// Back-compat alias used by older call sites during migration.
using PeerReachability = PeerPresenceStatus;

}  // namespace apptraverse::chat

#endif  // APPTRAVERSE_CHAT_PEER_SCHEDULE_H_
