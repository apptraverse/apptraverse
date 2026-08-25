#include <cctype>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include "aether/all.h"

#include "model/chat_room_local_state.h"
#include "model/registration.h"

#include "../common/aether_runtime.h"
#include "../common/startup_trace.h"
#include "threaded_room_runtime.h"
#include "win_chat_presenter.h"
#include "win_window_presenter.h"
#include "windows_window.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(WindowsWindow);
APPTRAVERSE_REGISTER(WinWindowPresenter);
APPTRAVERSE_REGISTER(WinChatPresenter);

}  // namespace
}  // namespace apptraverse

namespace {

struct CliOptions {
  apptraverse::examples::ThreadedRoomCliOptions runtime;
  bool distill{false};
  bool parse_error{false};
};

std::optional<ae::Uid> ParseUidArg(char const* text, char const* flag) {
  auto const uid = ae::Uid::FromString(std::string_view{text});
  if (uid.empty()) {
    std::cerr << "Invalid " << flag << " UID\n";
    return ae::Uid{};
  }
  return uid;
}

CliOptions ParseCli(int argc, char** argv) {
  CliOptions options;
  options.runtime.aether_client_name =
      apptraverse::examples::kWindowsAetherClientName;

  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    auto need_value = [&](char const* flag) -> char const* {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for " << flag << '\n';
        options.parse_error = true;
        return nullptr;
      }
      return argv[++i];
    };

    if (arg == "--distill") {
      options.distill = true;
    } else if (arg == "--state-dir") {
      if (auto const* value = need_value("--state-dir")) {
        options.runtime.state_dir = value;
      }
    } else if (arg == "--aether-client-name") {
      if (auto const* value = need_value("--aether-client-name")) {
        options.runtime.aether_client_name = value;
      }
    } else if (arg == "--print-aether-uid") {
      options.runtime.print_aether_uid = true;
    } else if (arg == "--role") {
      if (auto const* value = need_value("--role")) {
        std::string_view role{value};
        if (role == "host") {
          options.runtime.role = apptraverse::ChatRoomRole::kHost;
        } else if (role == "client") {
          options.runtime.role = apptraverse::ChatRoomRole::kClient;
        } else {
          std::cerr << "--role must be host or client\n";
          options.parse_error = true;
        }
      }
    } else if (arg == "--name") {
      if (auto const* value = need_value("--name")) {
        options.runtime.name = value;
      }
    } else if (arg == "--title") {
      if (auto const* value = need_value("--title")) {
        options.runtime.title = value;
      }
    } else if (arg == "--host-uid") {
      if (auto const* value = need_value("--host-uid")) {
        options.runtime.host_uid = ParseUidArg(value, "--host-uid");
        if (options.runtime.host_uid.has_value() &&
            options.runtime.host_uid->empty()) {
          options.parse_error = true;
        }
      }
    } else if (arg == "--send-after-active") {
      if (auto const* value = need_value("--send-after-active")) {
        options.runtime.send_after_active = value;
      }
    } else {
      std::cerr << "Unknown argument: " << arg << '\n';
      options.parse_error = true;
    }
  }

  if (options.distill) {
    return options;
  }

  if (!options.parse_error) {
    if (!options.runtime.role.has_value()) {
      std::cerr << "--role host|client is required\n";
      options.parse_error = true;
    }
    if (options.runtime.name.empty()) {
      std::cerr << "--name <display> is required\n";
      options.parse_error = true;
    }
    if (options.runtime.role == apptraverse::ChatRoomRole::kHost &&
        options.runtime.host_uid.has_value()) {
      std::cerr << "--host-uid is only valid with --role client\n";
      options.parse_error = true;
    }
  }
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  apptraverse::examples::StartupTraceReset();
  apptraverse::examples::RuntimeThreads().ui.store(
      std::this_thread::get_id(), std::memory_order::relaxed);
  apptraverse::examples::StartupTrace("APP_START");
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSingleClientChatRegistration();
  auto options = ParseCli(argc, argv);
  if (options.parse_error) {
    return 1;
  }
  if (options.distill) {
    std::cerr
        << "--distill is not supported: AppTraverse chat model is RAM-only "
           "and rebuilt every launch. Use --state-dir only for Aether "
           "DirectoryDomainStorage.\n";
    return 1;
  }
  return apptraverse::examples::RunThreadedRoomRuntime(options.runtime);
}
