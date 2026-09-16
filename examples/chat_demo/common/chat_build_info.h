#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_BUILD_INFO_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_BUILD_INFO_H_

#include <string>
#include <string_view>
#include <vector>

namespace apptraverse::example::chat_demo {

struct ChatBuildInfo {
  std::string source_sha;
  std::string compile_fingerprint;
  bool source_dirty{false};
  std::string source_dirty_flag;
  std::string configuration;
  std::string cxx_compiler;
  std::string aether_client_sha;
  std::string aether_objects_sha;
  std::string aether_miscpp_sha;
  std::string objects_scope_patch;
};

ChatBuildInfo GetChatBuildInfo();
std::string FormatChatBuildInfoText(ChatBuildInfo const& info);

// Handles --build-info / --build-info-file <path> before any profile/network work.
// Returns true when the process should exit with *exit_code (0 on success).
bool TryHandleBuildInfoArgs(std::vector<std::string> const& args, int* exit_code);
bool TryHandleBuildInfoArgs(int argc, char const* const* argv, int* exit_code);

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_BUILD_INFO_H_
