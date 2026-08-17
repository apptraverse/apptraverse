#ifndef APPTRAVERSE_EXAMPLES_AETHER_RX_POLICY_H_
#define APPTRAVERSE_EXAMPLES_AETHER_RX_POLICY_H_

#include <cassert>
#include <chrono>

#include "aether/all.h"

namespace apptraverse::examples {

// Host-level interactive receive-policy helper. Does not Save().
inline void ConfigureInteractiveAetherReceivePolicy(
    ae::Client::ptr client, std::chrono::milliseconds interval) {
  assert(client);
  auto policy = client->connectivity_policy();
  policy.Load();
  assert(policy.is_loaded());
  auto const duration = std::chrono::duration_cast<ae::Duration>(interval);
  policy->ConfigureRxTimings().ForAllPriorities(
      ae::RxTimingConf::Every(duration));
  policy->ResetRxTimings();
}

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_AETHER_RX_POLICY_H_