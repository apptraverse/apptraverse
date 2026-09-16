#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_CONNECTIVITY_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_CONNECTIVITY_H_

#include <cstdint>

namespace apptraverse::example::chat_demo {

// Local Aether connectivity from connectivity_policy().DiagnoseLocalPresence.
enum class LocalConnectivityState : std::uint8_t {
  kUnknown = 0,
  kOffline = 1,
  kOnline = 2,
};

inline LocalConnectivityState LocalConnectivityFromDiag(bool has_schedule,
                                                        bool any_online) {
  if (!has_schedule) {
    return LocalConnectivityState::kUnknown;
  }
  if (any_online) {
    return LocalConnectivityState::kOnline;
  }
  return LocalConnectivityState::kOffline;
}

enum class RoomBootstrapState : std::uint8_t {
  kNotStarted = 0,
  kPending = 1,
  kComplete = 2,
};

enum class MessageDeliveryState : std::uint8_t {
  kNone = 0,
  kQueued = 1,
  kSending = 2,
  kDelivered = 3,
};

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_CONNECTIVITY_H_
