#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_LAUNCH_IPC_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_LAUNCH_IPC_H_

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "chat_launch_options.h"

namespace apptraverse::example::chat_demo {

inline constexpr std::size_t kLaunchIpcMaxPayloadBytes = 65536;
inline constexpr std::uint32_t kLaunchIpcCopyDataMagic = 0x41544348;  // 'ATCH'

enum class LaunchIpcReply : std::uint32_t {
  kAccepted = 1,
  kRejected = 2,
  kProfileMismatch = 3,
  kInvalidPayload = 4,
  kOversized = 5,
};

enum class ForwardLaunchResult : std::uint8_t {
  kNoPrimary = 0,
  kAccepted = 1,
  kRejected = 2,
  kTimeout = 3,
  kError = 4,
};

struct ChatLaunchIpcPayload {
  std::string profile_key;
  OpenPeerRequest open_peer;
};

// Canonical absolute profile key used for lock ownership and IPC validation.
std::string NormalizeProfileKey(std::filesystem::path const& state_dir);

// Routing aid for locating the primary notification endpoint (hash only).
std::string ProfileRoutingToken(std::string const& profile_key);

#ifdef _WIN32
std::wstring ProfileRoutingWindowTitle(std::string const& profile_key);
std::wstring ProfileRoutingWindowClass(std::string const& profile_key);
#endif

bool EncodeLaunchIpcPayload(ChatLaunchIpcPayload const& payload,
                            std::vector<std::uint8_t>& out);

bool DecodeLaunchIpcPayload(std::vector<std::uint8_t> const& bytes,
                            ChatLaunchIpcPayload& out);

LaunchIpcReply ValidateLaunchIpcPayload(ChatLaunchIpcPayload const& payload,
                                        std::string const& expected_profile_key);

#ifdef _WIN32
ForwardLaunchResult TryForwardLaunchToPrimary(
    std::string const& profile_key, OpenPeerRequest const& open_peer,
    std::chrono::milliseconds timeout = std::chrono::milliseconds{3000},
    int max_attempts = 5);
#endif

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_LAUNCH_IPC_H_
