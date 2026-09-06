#ifndef APPTRAVERSE_UI_SEND_LATENCY_TRACKER_H_
#define APPTRAVERSE_UI_SEND_LATENCY_TRACKER_H_

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace chat {

// Runtime-only click→LISTBOX latency. Not persisted or shared.
class UiSendLatencyTracker {
 public:
  using Clock = std::chrono::steady_clock;

  std::uint64_t Begin(Clock::time_point t0) {
    std::lock_guard<std::mutex> lock{mu_};
    auto const id = ++next_trace_id_;
    pending_[id] = t0;
    return id;
  }

  void BindEvent(std::uint64_t trace_id, std::uint32_t local_event_obj_id) {
    std::lock_guard<std::mutex> lock{mu_};
    auto it = pending_.find(trace_id);
    if (it == pending_.end() || local_event_obj_id == 0) {
      return;
    }
    bound_[local_event_obj_id] = it->second;
    pending_.erase(it);
  }

  void Cancel(std::uint64_t trace_id) {
    std::lock_guard<std::mutex> lock{mu_};
    pending_.erase(trace_id);
  }

  // First resolve caches latency_ms; later resolves return the same value.
  std::optional<double> ResolveForPresentation(
      std::uint32_t local_event_obj_id, Clock::time_point t1) {
    std::lock_guard<std::mutex> lock{mu_};
    if (auto resolved = resolved_.find(local_event_obj_id);
        resolved != resolved_.end()) {
      return resolved->second;
    }
    auto it = bound_.find(local_event_obj_id);
    if (it == bound_.end()) {
      return std::nullopt;
    }
    auto const us =
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - it->second)
            .count();
    double const ms = static_cast<double>(us) / 1000.0;
    resolved_[local_event_obj_id] = ms;
    bound_.erase(it);
    return ms;
  }

  std::optional<double> CachedLatencyMs(
      std::uint32_t local_event_obj_id) const {
    std::lock_guard<std::mutex> lock{mu_};
    auto it = resolved_.find(local_event_obj_id);
    if (it == resolved_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

 private:
  mutable std::mutex mu_;
  std::uint64_t next_trace_id_{0};
  std::unordered_map<std::uint64_t, Clock::time_point> pending_;
  std::unordered_map<std::uint32_t, Clock::time_point> bound_;
  std::unordered_map<std::uint32_t, double> resolved_;
};

}  // namespace chat

#endif  // APPTRAVERSE_UI_SEND_LATENCY_TRACKER_H_
