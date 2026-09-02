/*
 * Copyright 2026 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef EXAMPLES_AETHER_PRESENCE_MONITOR_PRESENCE_REMOTE_STATE_H_
#define EXAMPLES_AETHER_PRESENCE_MONITOR_PRESENCE_REMOTE_STATE_H_

#include <cstdint>
#include <optional>
#include <string>

#include "aether/clock.h"
#include "aether/receive_schedule.h"

namespace presence_monitor {

enum class RemoteConnectivityState {
  kOnline,
  kSuspect,
  kOffline,
  kUnknown,
};

enum class QueryPhase {
  kIdle,
  kInFlight,
};

struct RemoteTimingSnapshot {
  ae::TimePoint last_online{};
  ae::TimePoint next_ping_deadline{};
  ae::PeerScheduleState raw_state{ae::PeerScheduleState::kUnknown};
};

struct RemoteStateSnapshot {
  RemoteConnectivityState derived{RemoteConnectivityState::kUnknown};
  ae::PeerScheduleState raw{ae::PeerScheduleState::kUnknown};
  QueryPhase query_phase{QueryPhase::kIdle};
  std::uint32_t stale_confirmation_count{0};
  ae::TimePoint last_online{};
  ae::TimePoint next_ping_deadline{};
  ae::TimePoint first_stale_confirmed_at{};
  ae::TimePoint query_started_at{};
  ae::TimePoint query_completed_at{};
  ae::TimePoint last_successful_query_at{};
  std::optional<RemoteTimingSnapshot> last_completed_result{};
  std::optional<int> last_query_error{};
  ae::Duration peer_ping_interval{};
};

inline char const* RemoteStateName(RemoteConnectivityState state) {
  switch (state) {
    case RemoteConnectivityState::kOnline:
      return "ONLINE";
    case RemoteConnectivityState::kSuspect:
      return "SUSPECT";
    case RemoteConnectivityState::kOffline:
      return "OFFLINE";
    case RemoteConnectivityState::kUnknown:
      return "UNKNOWN";
  }
  return "UNKNOWN";
}

class RemotePresenceTracker {
 public:
  explicit RemotePresenceTracker(ae::Duration peer_ping_interval)
      : peer_ping_interval_{peer_ping_interval} {}

  void SetPeerPingInterval(ae::Duration interval) {
    peer_ping_interval_ = interval;
  }

  ae::Duration peer_ping_interval() const noexcept { return peer_ping_interval_; }

  RemoteStateSnapshot const& snapshot() const noexcept { return snapshot_; }

  void BeginQuery(ae::TimePoint at) {
    snapshot_.query_phase = QueryPhase::kInFlight;
    snapshot_.query_started_at = at;
    snapshot_.last_query_error.reset();
  }

  void CompleteQuerySuccess(ae::TimePoint at, RemoteTimingSnapshot timing) {
    snapshot_.query_phase = QueryPhase::kIdle;
    snapshot_.query_completed_at = at;
    snapshot_.last_successful_query_at = at;
    snapshot_.last_completed_result = timing;
    snapshot_.last_query_error.reset();
    snapshot_.raw = timing.raw_state;

    if (timing.raw_state == ae::PeerScheduleState::kExpected) {
      snapshot_.derived = RemoteConnectivityState::kOnline;
      snapshot_.stale_confirmation_count = 0;
      snapshot_.first_stale_confirmed_at = ae::TimePoint{};
      if (timing.last_online != ae::TimePoint{}) {
        snapshot_.last_online = timing.last_online;
      }
      if (timing.next_ping_deadline != ae::TimePoint{}) {
        snapshot_.next_ping_deadline = timing.next_ping_deadline;
      }
      return;
    }

    if (timing.raw_state == ae::PeerScheduleState::kMissedDeadline) {
      if (snapshot_.stale_confirmation_count == 0) {
        snapshot_.derived = RemoteConnectivityState::kSuspect;
        snapshot_.stale_confirmation_count = 1;
        snapshot_.first_stale_confirmed_at = at;
      } else if (snapshot_.first_stale_confirmed_at != ae::TimePoint{} &&
                 at - snapshot_.first_stale_confirmed_at >=
                     peer_ping_interval_) {
        snapshot_.derived = RemoteConnectivityState::kOffline;
        snapshot_.stale_confirmation_count = 2;
      } else {
        snapshot_.derived = RemoteConnectivityState::kSuspect;
      }
      if (timing.last_online != ae::TimePoint{}) {
        snapshot_.last_online = timing.last_online;
      }
      if (timing.next_ping_deadline != ae::TimePoint{}) {
        snapshot_.next_ping_deadline = timing.next_ping_deadline;
      }
      return;
    }

    snapshot_.derived = RemoteConnectivityState::kUnknown;
  }

  void CompleteQueryError(ae::TimePoint at, int error) {
    snapshot_.query_phase = QueryPhase::kIdle;
    snapshot_.query_completed_at = at;
    snapshot_.derived = RemoteConnectivityState::kUnknown;
    snapshot_.last_query_error = error;
    snapshot_.raw = ae::PeerScheduleState::kUnknown;
  }

  ae::Duration TimeUntilOfflineConfirmationAllowed(ae::TimePoint now) const {
    if (snapshot_.stale_confirmation_count == 0 ||
        snapshot_.first_stale_confirmed_at == ae::TimePoint{}) {
      return ae::Duration{};
    }
    if (now >= snapshot_.first_stale_confirmed_at + peer_ping_interval_) {
      return ae::Duration{};
    }
    return snapshot_.first_stale_confirmed_at + peer_ping_interval_ - now;
  }

 private:
  ae::Duration peer_ping_interval_{};
  RemoteStateSnapshot snapshot_{};
};

}  // namespace presence_monitor

#endif  // EXAMPLES_AETHER_PRESENCE_MONITOR_PRESENCE_REMOTE_STATE_H_
