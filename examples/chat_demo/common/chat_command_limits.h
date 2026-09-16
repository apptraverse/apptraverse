#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_COMMAND_LIMITS_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_COMMAND_LIMITS_H_

#include <cstddef>
#include <string>

namespace apptraverse::example::chat_demo {

// User-command ingress limits (aligned with launch IPC bounds where applicable).
inline constexpr std::size_t kMaxPeerAdminIdBytes = 1024;
inline constexpr std::size_t kMaxPeerAetherUidBytes = 128;
inline constexpr std::size_t kMaxPeerNameBytes = 1024;
// Draft text bound below Aether application-frame payload cap (16 MiB).
inline constexpr std::size_t kMaxDraftTextBytes = 65536;

inline bool FieldWithinUserCommandLimit(std::string const& value,
                                        std::size_t max_bytes) {
  return value.size() <= max_bytes;
}

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_COMMAND_LIMITS_H_
