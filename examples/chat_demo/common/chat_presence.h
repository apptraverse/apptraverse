#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_PRESENCE_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_PRESENCE_H_

#include <cstdint>

namespace apptraverse::example::chat_demo {

enum class PeerPresence : std::uint8_t {
  kUnknown = 0,
  kConnecting = 1,
  kOnline = 2,
  kOffline = 3,
};

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_PRESENCE_H_
