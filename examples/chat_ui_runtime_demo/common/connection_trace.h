#ifndef APPTRAVERSE_CONNECTION_TRACE_H_
#define APPTRAVERSE_CONNECTION_TRACE_H_

#include <chrono>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace apptraverse {

// Lightweight steady-clock stage log for connection / convergence timing.
class ConnectionTrace {
 public:
  using Clock = std::chrono::steady_clock;

  void Mark(std::string_view stage) {
    stages_.emplace_back(std::string{stage}, Clock::now());
  }

  void Dump(std::ostream& os) const {
    if (stages_.empty()) {
      os << "ConnectionTrace: (empty)\n";
      return;
    }
    auto const t0 = stages_.front().second;
    os << "ConnectionTrace stages:\n";
    for (std::size_t i = 0; i < stages_.size(); ++i) {
      auto const& [tag, tp] = stages_[i];
      auto const from_start_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(tp - t0)
              .count();
      os << "  [" << i << "] " << tag << " +" << from_start_ms << "ms";
      if (i > 0) {
        auto const delta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  tp - stages_[i - 1].second)
                                  .count();
        os << " (delta " << delta_ms << "ms)";
      }
      os << '\n';
    }
  }

  // Elapsed ms between first occurrence of from_stage and to_stage.
  // Returns -1 if either stage is missing.
  std::int64_t ElapsedMs(std::string_view from_stage,
                         std::string_view to_stage) const {
    Clock::time_point from{};
    Clock::time_point to{};
    bool have_from = false;
    bool have_to = false;
    for (auto const& [tag, tp] : stages_) {
      if (!have_from && tag == from_stage) {
        from = tp;
        have_from = true;
      }
      if (have_from && tag == to_stage) {
        to = tp;
        have_to = true;
        break;
      }
    }
    if (!have_from || !have_to) {
      return -1;
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(to - from)
        .count();
  }

 private:
  std::vector<std::pair<std::string, Clock::time_point>> stages_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CONNECTION_TRACE_H_
