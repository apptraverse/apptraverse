#ifndef APPTRAVERSE_REMOTE_PRESENCE_POLLER_H_
#define APPTRAVERSE_REMOTE_PRESENCE_POLLER_H_

#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace apptraverse {

// Schedules remote peer presence queries. One outstanding query per UID.
// Does not talk to Aether directly — caller supplies start_query / cancel.
class RemotePresencePoller {
 public:
  using Clock = std::chrono::steady_clock;
  using StartQueryFn = std::function<bool(std::string const& remote_uid)>;

  static constexpr auto kPeriod = std::chrono::seconds{1};

  // Idempotent. Self UID is ignored when local_uid is set.
  void Monitor(std::string remote_uid, std::string const& local_uid = {}) {
    if (remote_uid.empty() || remote_uid == local_uid) {
      return;
    }
    auto& slot = slots_[remote_uid];
    slot.uid = remote_uid;
    if (!slot.armed) {
      slot.armed = true;
      slot.next_due = Clock::time_point{};  // immediate
    }
  }

  void Stop() {
    slots_.clear();
    outstanding_.clear();
  }

  // Starts due queries. start_query returns false to defer (not marked outstanding).
  void Tick(Clock::time_point now, StartQueryFn const& start_query) {
    if (!start_query) {
      return;
    }
    for (auto& [uid, slot] : slots_) {
      if (outstanding_.count(uid) != 0) {
        continue;
      }
      if (slot.next_due.time_since_epoch().count() != 0 && now < slot.next_due) {
        continue;
      }
      if (start_query(uid)) {
        outstanding_.insert(uid);
      }
    }
  }

  // Call when a query finishes (success or failure). Schedules next period.
  void OnQueryFinished(std::string const& remote_uid, Clock::time_point now) {
    outstanding_.erase(remote_uid);
    auto it = slots_.find(remote_uid);
    if (it == slots_.end()) {
      return;
    }
    it->second.next_due = now + kPeriod;
  }

  bool HasOutstanding(std::string const& remote_uid) const {
    return outstanding_.count(remote_uid) != 0;
  }

  std::size_t MonitoredCount() const { return slots_.size(); }

  std::vector<std::string> MonitoredUids() const {
    std::vector<std::string> out;
    out.reserve(slots_.size());
    for (auto const& [uid, _] : slots_) {
      out.push_back(uid);
    }
    return out;
  }

 private:
  struct Slot {
    std::string uid;
    bool armed{false};
    Clock::time_point next_due{};
  };

  std::unordered_map<std::string, Slot> slots_;
  std::unordered_set<std::string> outstanding_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_REMOTE_PRESENCE_POLLER_H_
