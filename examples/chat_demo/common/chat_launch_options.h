#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_LAUNCH_OPTIONS_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_LAUNCH_OPTIONS_H_

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace apptraverse::example::chat_demo {

// AeroAdmin peer opening request.
struct OpenPeerRequest {
  std::string peer_admin_id;
  std::optional<std::string> peer_aether_uid;
  std::optional<std::string> peer_name;

  bool operator==(OpenPeerRequest const& other) const noexcept = default;
};

// Parsed desktop launch options.
struct ChatLaunchOptions {
  std::optional<std::string> state_dir;
  std::optional<OpenPeerRequest> open_peer;

  bool operator==(ChatLaunchOptions const& other) const noexcept = default;
};

// Result of parsing command line launch options.
struct ParseLaunchOptionsResult {
  bool ok{false};
  ChatLaunchOptions options;
  std::string error_message;
};

// Parses command-line arguments (excluding executable name).
ParseLaunchOptionsResult ParseChatLaunchOptions(
    std::span<std::string_view const> args);

// Helper overload accepting vector of strings.
ParseLaunchOptionsResult ParseChatLaunchOptions(
    std::vector<std::string> const& args);

// Helper overload accepting argc/argv (where argv[0] is program name).
ParseLaunchOptionsResult ParseChatLaunchOptions(int argc, char const* const* argv);

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_LAUNCH_OPTIONS_H_
