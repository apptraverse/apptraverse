#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_LAUNCH_OPTIONS_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_LAUNCH_OPTIONS_H_

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "chat_model.h"

namespace apptraverse::example::chat_demo {

struct ChatLaunchOptions {
  DemoRole role{DemoRole::kUnconfigured};
  std::optional<std::string> state_dir;
  std::optional<std::string> host_uid_prefill;
  bool show_help{false};
};

struct ParseChatLaunchOptionsResult {
  bool ok{false};
  ChatLaunchOptions options;
  std::string error_message;
};

std::string ChatLaunchUsageText();

std::filesystem::path DefaultChatExampleStateDir(DemoRole role);

ParseChatLaunchOptionsResult ParseChatLaunchOptions(
    std::vector<std::string> const& args);
ParseChatLaunchOptionsResult ParseChatLaunchOptions(int argc,
                                                    char const* const* argv);

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_LAUNCH_OPTIONS_H_
