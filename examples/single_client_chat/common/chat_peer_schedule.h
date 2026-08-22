#ifndef APPTRAVERSE_CHAT_PEER_SCHEDULE_H_
#define APPTRAVERSE_CHAT_PEER_SCHEDULE_H_

#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string_view>

namespace ae {
struct Uid;
}

namespace apptraverse::chat {

inline constexpr std::string_view kOfflinePingMarker{"\xE2\x8F\xB1"};
inline constexpr std::string_view kOfflineMissedVisitMarker = kOfflinePingMarker;
inline constexpr std::int64_t kPeerScheduleGraceMs = 500;

enum class PeerReachability : std::uint8_t {
  kUnknown = 0,
  kOnline = 1,
  kWaitingForScheduledPing = 2,
  kScheduleCheckPending = 3,
  kOfflineMissedPing = 4,
  kOfflineNoFuturePing = 5,
};

struct PeerScheduleSnapshot {
  std::int64_t last_ping_server_ms{0};
  std::int64_t next_ping_delta_ms{0};
  std::optional<std::chrono::steady_clock::time_point> local_deadline;
};

using PeerScheduleQueryCallback =
    std::function<void(std::optional<PeerScheduleSnapshot>)>;
using QueryPeerScheduleFunction =
    std::function<void(ae::Uid const& peer, PeerScheduleQueryCallback)>;

inline bool ScheduleQueryFailed(
    std::optional<PeerScheduleSnapshot> const& r) {
  return !r.has_value();
}

inline std::int64_t SaturatingAddI64(std::int64_t a, std::int64_t b) noexcept {
  auto const max_v = std::numeric_limits<std::int64_t>::max();
  auto const min_v = std::numeric_limits<std::int64_t>::min();
  if (b >= 0) {
    if (a > max_v - b) {
      return max_v;
    }
  } else if (a < min_v - b) {
    return min_v;
  }
  return a + b;
}

inline std::int64_t SaturatingSubI64(std::int64_t a, std::int64_t b) noexcept {
  auto const max_v = std::numeric_limits<std::int64_t>::max();
  auto const min_v = std::numeric_limits<std::int64_t>::min();
  if (b >= 0) {
    if (a < min_v + b) {
      return min_v;
    }
  } else if (a > max_v + b) {
    return max_v;
  }
  return a - b;
}

inline PeerScheduleSnapshot MakePeerScheduleSnapshot(
    std::int64_t last_ping_server_ms, std::int64_t next_ping_delta_ms,
    std::int64_t server_now_ms,
    std::chrono::steady_clock::time_point steady_now =
        std::chrono::steady_clock::now()) {
  PeerScheduleSnapshot snap{};
  snap.last_ping_server_ms = last_ping_server_ms;
  snap.next_ping_delta_ms = next_ping_delta_ms;
  if (next_ping_delta_ms <= 0) {
    return snap;
  }
  auto remaining =
      SaturatingSubI64(SaturatingAddI64(last_ping_server_ms, next_ping_delta_ms),
                       server_now_ms);
  if (remaining < 0) {
    remaining = 0;
  }
  snap.local_deadline = steady_now + std::chrono::milliseconds{remaining} +
                        std::chrono::milliseconds{kPeerScheduleGraceMs};
  return snap;
}

inline bool ConfirmedOfflineHold(PeerReachability reachability) noexcept {
  return reachability == PeerReachability::kOfflineMissedPing ||
         reachability == PeerReachability::kOfflineNoFuturePing;
}

inline bool ShowOfflinePingMarker(PeerReachability reachability) noexcept {
  return ConfirmedOfflineHold(reachability);
}

}  // namespace apptraverse::chat

#endif  // APPTRAVERSE_CHAT_PEER_SCHEDULE_H_
