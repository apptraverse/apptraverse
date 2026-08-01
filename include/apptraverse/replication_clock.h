#ifndef APPTRAVERSE_REPLICATION_CLOCK_H_
#define APPTRAVERSE_REPLICATION_CLOCK_H_

#include <chrono>
#include <cstdint>

namespace apptraverse {

class IReplicationClock {
 public:
  virtual ~IReplicationClock() = default;

  // Unix UTC time in microseconds since epoch.
  virtual std::uint64_t NowMicroseconds() = 0;
};

class SystemReplicationClock final : public IReplicationClock {
 public:
  std::uint64_t NowMicroseconds() override {
    using clock = std::chrono::system_clock;
    auto const now = clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_REPLICATION_CLOCK_H_
