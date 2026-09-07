#ifndef CHAT_AETHER_CLIENT_INIT_H_
#define CHAT_AETHER_CLIENT_INIT_H_

#include <chrono>
#include <cstdint>
#include <optional>

#include "apptraverse/runtime_lifecycle.h"

namespace chat {

// Pure decision logic for first-run / recovery SelectClient orchestration.
// Aether API calls stay on ChatAetherRuntime's Aether thread; this type only
// answers when to start or retry SelectClient.
enum class AetherClientInitState : std::uint8_t {
  kWaitingForNetwork = 0,
  kSelecting = 1,
  kRetryDelay = 2,
  kReady = 3,
};

class AetherClientInitController {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;
  using Duration = std::chrono::milliseconds;

  AetherClientInitState state() const { return state_; }
  bool is_ready() const { return state_ == AetherClientInitState::kReady; }
  bool is_selecting() const {
    return state_ == AetherClientInitState::kSelecting;
  }

  // Returns true when the caller should start SelectClient immediately.
  bool OnNetworkObservation(apptraverse::NetworkAvailability availability,
                            TimePoint now) {
    bool const available =
        availability == apptraverse::NetworkAvailability::kAvailable;
    bool const was_available = network_available_;
    network_available_ = available;

    if (!available) {
      retry_at_.reset();
      if (state_ == AetherClientInitState::kReady) {
        return false;
      }
      // In-flight SelectClient is left running; on failure we will wait.
      if (state_ != AetherClientInitState::kSelecting) {
        state_ = AetherClientInitState::kWaitingForNetwork;
      }
      return false;
    }

    if (state_ == AetherClientInitState::kReady ||
        state_ == AetherClientInitState::kSelecting) {
      return false;
    }

    if (!was_available) {
      // unavailable -> available: reset backoff and start immediately.
      failure_count_ = 0;
      retry_at_.reset();
      return true;
    }

    if (state_ == AetherClientInitState::kWaitingForNetwork) {
      return true;
    }

    if (state_ == AetherClientInitState::kRetryDelay) {
      return Tick(now);
    }
    return false;
  }

  void OnSelectClientStarted() {
    state_ = AetherClientInitState::kSelecting;
    retry_at_.reset();
  }

  void OnSelectClientSucceeded() {
    state_ = AetherClientInitState::kReady;
    retry_at_.reset();
    failure_count_ = 0;
  }

  // Returns true when SelectClient should be started again immediately.
  bool OnSelectClientFailed(TimePoint now) {
    if (state_ == AetherClientInitState::kReady) {
      return false;
    }
    if (!network_available_) {
      state_ = AetherClientInitState::kWaitingForNetwork;
      retry_at_.reset();
      return false;
    }
    ScheduleRetry(now);
    return false;
  }

  // Returns true when a scheduled retry is due.
  bool Tick(TimePoint now) {
    if (state_ != AetherClientInitState::kRetryDelay) {
      return false;
    }
    if (!network_available_) {
      state_ = AetherClientInitState::kWaitingForNetwork;
      retry_at_.reset();
      return false;
    }
    if (!retry_at_.has_value() || now < *retry_at_) {
      return false;
    }
    retry_at_.reset();
    return true;
  }

  std::optional<TimePoint> retry_at() const { return retry_at_; }
  std::uint32_t failure_count() const { return failure_count_; }

  static Duration RetryDelayForFailureCount(std::uint32_t failures) {
    // 500ms, 1s, 2s, then cap at 5s.
    if (failures <= 1) {
      return Duration{500};
    }
    if (failures == 2) {
      return Duration{1000};
    }
    if (failures == 3) {
      return Duration{2000};
    }
    return Duration{5000};
  }

 private:
  void ScheduleRetry(TimePoint now) {
    ++failure_count_;
    state_ = AetherClientInitState::kRetryDelay;
    retry_at_ = now + RetryDelayForFailureCount(failure_count_);
  }

  AetherClientInitState state_{AetherClientInitState::kWaitingForNetwork};
  bool network_available_{false};
  std::uint32_t failure_count_{0};
  std::optional<TimePoint> retry_at_;
};

}  // namespace chat

#endif  // CHAT_AETHER_CLIENT_INIT_H_
