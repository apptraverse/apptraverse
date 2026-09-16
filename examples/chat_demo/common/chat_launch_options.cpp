#include "chat_launch_options.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace apptraverse::example::chat_demo {
namespace {

bool NextArgIsOption(std::span<std::string_view const> args, std::size_t i) {
  if (i + 1 >= args.size()) {
    return false;
  }
  std::string_view const next = args[i + 1];
  return next.size() >= 2 && next[0] == '-' && next[1] == '-';
}

}  // namespace

ParseLaunchOptionsResult ParseChatLaunchOptions(
    std::span<std::string_view const> args) {
  ChatLaunchOptions options;
  std::optional<std::string> state_dir;
  std::optional<std::string> peer_admin_id;
  std::optional<std::string> peer_aether_uid;
  std::optional<std::string> peer_name;

  for (std::size_t i = 0; i < args.size(); ++i) {
    std::string_view const arg = args[i];

    if (arg == "--state-dir") {
      if (state_dir.has_value()) {
        return {.ok = false, .error_message = "Repeated option: --state-dir"};
      }
      if (i + 1 >= args.size() || NextArgIsOption(args, i)) {
        return {.ok = false, .error_message = "Missing value for --state-dir"};
      }
      ++i;
      state_dir = std::string(args[i]);
    } else if (arg == "--peer-admin-id") {
      if (peer_admin_id.has_value()) {
        return {.ok = false, .error_message = "Repeated option: --peer-admin-id"};
      }
      if (i + 1 >= args.size() || NextArgIsOption(args, i)) {
        return {.ok = false,
                .error_message = "Missing value for --peer-admin-id"};
      }
      ++i;
      peer_admin_id = std::string(args[i]);
    } else if (arg == "--peer-aether-uid") {
      if (peer_aether_uid.has_value()) {
        return {.ok = false,
                .error_message = "Repeated option: --peer-aether-uid"};
      }
      if (i + 1 >= args.size() || NextArgIsOption(args, i)) {
        return {.ok = false,
                .error_message = "Missing value for --peer-aether-uid"};
      }
      ++i;
      peer_aether_uid = std::string(args[i]);
    } else if (arg == "--peer-name") {
      if (peer_name.has_value()) {
        return {.ok = false, .error_message = "Repeated option: --peer-name"};
      }
      if (i + 1 >= args.size() || NextArgIsOption(args, i)) {
        return {.ok = false, .error_message = "Missing value for --peer-name"};
      }
      ++i;
      peer_name = std::string(args[i]);
    } else {
      return {.ok = false,
              .error_message = "Unknown option: " + std::string(arg)};
    }
  }

  // Validate peer options: peer UID or name without peer Admin ID is an error
  if (!peer_admin_id.has_value()) {
    if (peer_aether_uid.has_value()) {
      return {.ok = false,
              .error_message =
                  "--peer-aether-uid specified without --peer-admin-id"};
    }
    if (peer_name.has_value()) {
      return {.ok = false,
              .error_message = "--peer-name specified without --peer-admin-id"};
    }
  }

  options.state_dir = std::move(state_dir);
  if (peer_admin_id.has_value()) {
    options.open_peer = OpenPeerRequest{
        .peer_admin_id = std::move(*peer_admin_id),
        .peer_aether_uid = std::move(peer_aether_uid),
        .peer_name = std::move(peer_name),
    };
  }

  return {.ok = true, .options = std::move(options)};
}

ParseLaunchOptionsResult ParseChatLaunchOptions(
    std::vector<std::string> const& args) {
  std::vector<std::string_view> views;
  views.reserve(args.size());
  for (auto const& s : args) {
    views.emplace_back(s);
  }
  return ParseChatLaunchOptions(views);
}

ParseLaunchOptionsResult ParseChatLaunchOptions(int argc,
                                                char const* const* argv) {
  std::vector<std::string_view> views;
  if (argc > 1 && argv != nullptr) {
    views.reserve(argc - 1);
    for (int i = 1; i < argc; ++i) {
      if (argv[i] != nullptr) {
        views.emplace_back(argv[i]);
      }
    }
  }
  return ParseChatLaunchOptions(views);
}

}  // namespace apptraverse::example::chat_demo
