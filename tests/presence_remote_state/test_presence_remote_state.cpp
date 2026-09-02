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

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>

#include "aether/clock.h"
#include "aether/receive_schedule.h"

#include "examples/aether_presence_monitor/presence_remote_state.h"

namespace {

ae::TimePoint Tp(std::uint32_t ms) {
  return ae::TimePoint{} + std::chrono::milliseconds{ms};
}

ae::Duration Ms(std::uint32_t v) {
  return std::chrono::duration_cast<ae::Duration>(std::chrono::milliseconds{v});
}

void test_expected_is_online() {
  presence_monitor::RemotePresenceTracker tracker{Ms(1000)};
  tracker.BeginQuery(Tp(0));
  tracker.CompleteQuerySuccess(
      Tp(10),
      presence_monitor::RemoteTimingSnapshot{
          .last_online = Tp(5),
          .next_ping_deadline = Tp(1000),
          .raw_state = ae::PeerScheduleState::kExpected});
  assert(tracker.snapshot().derived ==
         presence_monitor::RemoteConnectivityState::kOnline);
}

void test_first_missed_is_suspect() {
  presence_monitor::RemotePresenceTracker tracker{Ms(1000)};
  tracker.CompleteQuerySuccess(
      Tp(10),
      presence_monitor::RemoteTimingSnapshot{
          .raw_state = ae::PeerScheduleState::kMissedDeadline});
  assert(tracker.snapshot().derived ==
         presence_monitor::RemoteConnectivityState::kSuspect);
  assert(tracker.snapshot().stale_confirmation_count == 1);
}

void test_second_early_missed_stays_suspect() {
  presence_monitor::RemotePresenceTracker tracker{Ms(1000)};
  tracker.CompleteQuerySuccess(
      Tp(10),
      presence_monitor::RemoteTimingSnapshot{
          .raw_state = ae::PeerScheduleState::kMissedDeadline});
  tracker.CompleteQuerySuccess(
      Tp(100),
      presence_monitor::RemoteTimingSnapshot{
          .raw_state = ae::PeerScheduleState::kMissedDeadline});
  assert(tracker.snapshot().derived ==
         presence_monitor::RemoteConnectivityState::kSuspect);
}

void test_second_after_interval_is_offline() {
  presence_monitor::RemotePresenceTracker tracker{Ms(1000)};
  tracker.CompleteQuerySuccess(
      Tp(10),
      presence_monitor::RemoteTimingSnapshot{
          .raw_state = ae::PeerScheduleState::kMissedDeadline});
  tracker.CompleteQuerySuccess(
      Tp(1100),
      presence_monitor::RemoteTimingSnapshot{
          .raw_state = ae::PeerScheduleState::kMissedDeadline});
  assert(tracker.snapshot().derived ==
         presence_monitor::RemoteConnectivityState::kOffline);
}

void test_expected_resets_stale() {
  presence_monitor::RemotePresenceTracker tracker{Ms(1000)};
  tracker.CompleteQuerySuccess(
      Tp(10),
      presence_monitor::RemoteTimingSnapshot{
          .raw_state = ae::PeerScheduleState::kMissedDeadline});
  tracker.CompleteQuerySuccess(
      Tp(20),
      presence_monitor::RemoteTimingSnapshot{
          .raw_state = ae::PeerScheduleState::kExpected});
  assert(tracker.snapshot().stale_confirmation_count == 0);
}

void test_query_error_is_unknown() {
  presence_monitor::RemotePresenceTracker tracker{Ms(1000)};
  tracker.CompleteQuerySuccess(
      Tp(10),
      presence_monitor::RemoteTimingSnapshot{
          .raw_state = ae::PeerScheduleState::kExpected});
  tracker.BeginQuery(Tp(20));
  tracker.CompleteQueryError(Tp(30), 3);
  assert(tracker.snapshot().derived ==
         presence_monitor::RemoteConnectivityState::kUnknown);
  assert(tracker.snapshot().last_completed_result.has_value());
}

void test_inflight_keeps_last_completed() {
  presence_monitor::RemotePresenceTracker tracker{Ms(1000)};
  tracker.CompleteQuerySuccess(
      Tp(10),
      presence_monitor::RemoteTimingSnapshot{
          .raw_state = ae::PeerScheduleState::kExpected});
  tracker.BeginQuery(Tp(20));
  assert(tracker.snapshot().query_phase ==
         presence_monitor::QueryPhase::kInFlight);
  assert(tracker.snapshot().derived ==
         presence_monitor::RemoteConnectivityState::kOnline);
}

}  // namespace

int main() {
  test_expected_is_online();
  test_first_missed_is_suspect();
  test_second_early_missed_stays_suspect();
  test_second_after_interval_is_offline();
  test_expected_resets_stale();
  test_query_error_is_unknown();
  test_inflight_keeps_last_completed();
  std::cout << "presence_remote_state_test: PASS\n";
  return EXIT_SUCCESS;
}
