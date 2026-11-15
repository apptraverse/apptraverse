#include "apptraverse/connection_runtime.h"

#include <chrono>
#include <cstdlib>
namespace apptraverse {
ConnectionTime ReadConnectionTime() {
  return {static_cast<std::uint64_t>(
              std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::steady_clock::now().time_since_epoch())
                  .count()),
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::system_clock::now().time_since_epoch())
              .count()};
}
ConnectionRuntime::ConnectionRuntime(ConnectionAdapters a)
    : adapters_{std::move(a)} {
  if (!adapters_.transport || !adapters_.presence) std::abort();
}
void ConnectionRuntime::NetworkChanged(NetworkObservation v) {
  adapters_.transport->NetworkChanged(v);
  adapters_.presence->NetworkChanged(v);
}
void ConnectionRuntime::Service(ConnectionTime now) {
  now_us_ = now.monotonic_us;
  adapters_.transport->Service(now);
  adapters_.presence->Service(now);
}
SessionObservation ConnectionRuntime::Observation() const {
  SessionObservation result{adapters_.transport->Name(),
                            adapters_.transport->EndpointId(),
                            adapters_.transport->Phase(),
                            {},
                            false,
                            0,
                            0};
  for (auto sample : adapters_.presence->Observations()) {
    if (sample.value.valid_for_us == 0 ||
        now_us_ < sample.observed_monotonic_us ||
        now_us_ - sample.observed_monotonic_us >= sample.value.valid_for_us)
      sample.value.state = PresenceState::Unknown;
    result.presence.push_back(std::move(sample.value));
  }
  return result;
}
}  // namespace apptraverse
