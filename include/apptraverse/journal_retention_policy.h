#ifndef APPTRAVERSE_JOURNAL_RETENTION_POLICY_H_
#define APPTRAVERSE_JOURNAL_RETENTION_POLICY_H_

#include <chrono>
#include <cstddef>
#include <limits>
#include <optional>

namespace apptraverse {

// Replay/debug retention for a Node journal. Independent of synchronization
// hold (compaction blocked). Default preserves historical unlimited journals.
struct JournalRetentionPolicy {
  // Keep every event by count. Distinct from max_events == 0 (retain none).
  static constexpr std::size_t kUnlimitedEvents =
      (std::numeric_limits<std::size_t>::max)();

  std::size_t max_events{kUnlimitedEvents};
  // Absent: age retention unused. Present: retain if
  // now_us - retained_since_us <= max_age (inclusive).
  std::optional<std::chrono::microseconds> max_age{};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_JOURNAL_RETENTION_POLICY_H_
