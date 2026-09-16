#include "chat_launch_options.h"

#include <cstdlib>
#include <string_view>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <shlobj.h>
#  include <windows.h>
#endif

namespace apptraverse::example::chat_demo {
namespace {

bool EqualsFlag(std::string_view arg, std::string_view flag) {
  return arg == flag;
}

bool IsRetiredFlag(std::string_view arg) {
  return arg == "--peer-admin-id" || arg == "--peer-aether-uid" ||
         arg == "--peer-name" || arg == "--open-peer" || arg == "--admin-id";
}

}  // namespace

std::string ChatLaunchUsageText() {
  return "Usage: apptraverse_chat --host|--client [--state-dir <path>] "
         "[--host-uid <uid>]\n"
         "  --host              run as Host\n"
         "  --client            run as Client\n"
         "  --state-dir <path>  profile directory (default: native user data / "
         "App Traverse / ChatExample / host|client)\n"
         "  --host-uid <uid>    Client only: prefill Host UID (does not Join)\n"
         "  --help              show this help\n";
}

std::filesystem::path DefaultChatExampleStateDir(DemoRole role) {
  std::filesystem::path root;
#ifdef _WIN32
  PWSTR path = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
    root = std::filesystem::path{path};
    CoTaskMemFree(path);
  } else {
    root = std::filesystem::current_path();
  }
#else
  char const* xdg = std::getenv("XDG_DATA_HOME");
  if (xdg != nullptr && xdg[0] != '\0') {
    root = std::filesystem::path{xdg};
  } else {
    char const* home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0') {
      root = std::filesystem::path{home} / ".local" / "share";
    } else {
      root = std::filesystem::current_path();
    }
  }
#endif
  char const* role_dir = role == DemoRole::kClient ? "client" : "host";
  return root / "App Traverse" / "ChatExample" / role_dir;
}

ParseChatLaunchOptionsResult ParseChatLaunchOptions(
    std::vector<std::string> const& args) {
  ParseChatLaunchOptionsResult result;
  bool saw_host = false;
  bool saw_client = false;
  bool saw_state_dir = false;
  bool saw_host_uid = false;

  for (std::size_t i = 0; i < args.size(); ++i) {
    std::string const& arg = args[i];
    if (EqualsFlag(arg, "--help") || EqualsFlag(arg, "-h")) {
      result.options.show_help = true;
      continue;
    }
    if (EqualsFlag(arg, "--host")) {
      saw_host = true;
      continue;
    }
    if (EqualsFlag(arg, "--client")) {
      saw_client = true;
      continue;
    }
    if (EqualsFlag(arg, "--state-dir")) {
      if (saw_state_dir) {
        result.error_message = "Repeated option: --state-dir";
        return result;
      }
      saw_state_dir = true;
      if (i + 1 >= args.size()) {
        result.error_message = "Missing value for --state-dir";
        return result;
      }
      result.options.state_dir = args[++i];
      continue;
    }
    if (EqualsFlag(arg, "--host-uid")) {
      if (saw_host_uid) {
        result.error_message = "Repeated option: --host-uid";
        return result;
      }
      saw_host_uid = true;
      if (i + 1 >= args.size()) {
        result.error_message = "Missing value for --host-uid";
        return result;
      }
      result.options.host_uid_prefill = args[++i];
      continue;
    }
    if (IsRetiredFlag(arg)) {
      result.error_message = "Unsupported option: " + arg +
                     " (use --host/--client and --host-uid)";
      return result;
    }
    result.error_message = "Unknown option: " + arg;
    return result;
  }

  if (result.options.show_help) {
    result.ok = true;
    return result;
  }

  if (saw_host && saw_client) {
    result.error_message = "Specify exactly one of --host or --client";
    return result;
  }
  if (!saw_host && !saw_client) {
    result.error_message = "Specify --host or --client";
    return result;
  }
  result.options.role = saw_host ? DemoRole::kHost : DemoRole::kClient;

  if (result.options.host_uid_prefill.has_value() &&
      result.options.role != DemoRole::kClient) {
    result.error_message = "--host-uid is allowed only with --client";
    return result;
  }

  result.ok = true;
  return result;
}

ParseChatLaunchOptionsResult ParseChatLaunchOptions(int argc,
                                                    char const* const* argv) {
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    args.emplace_back(argv[i] != nullptr ? argv[i] : "");
  }
  return ParseChatLaunchOptions(args);
}

}  // namespace apptraverse::example::chat_demo
