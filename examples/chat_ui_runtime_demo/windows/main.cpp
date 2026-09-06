#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include "chat_bootstrap.h"
#include "chat_ids.h"
#include "chat_model.h"
#include "win_app.h"
#include "win_presenters.h"

namespace {

void PrintUsage() {
  std::cerr
      << "usage:\n"
      << "  win32_chat_ui_runtime_demo.exe --state-dir <dir> [--host] "
         "[--name NAME]\n"
      << "  win32_chat_ui_runtime_demo.exe --state-dir <dir> --client "
         "--name NAME\n"
      << "\n"
      << "Existing state is loaded; --host/--client/--name are ignored.\n"
      << "Join room is not implemented in this milestone.\n";
}

}  // namespace

int main(int argc, char** argv) {
  bool host = false;
  bool client = false;
  bool have_state_dir = false;
  bool have_name = false;
  std::filesystem::path state_dir;
  std::string display_name;
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
      have_state_dir = true;
    } else if (arg == "--name" && i + 1 < argc) {
      display_name = argv[++i];
      have_name = true;
    } else if (arg == "--host") {
      host = true;
    } else if (arg == "--client") {
      client = true;
    } else if (arg == "--help" || arg == "-h") {
      PrintUsage();
      return 0;
    }
  }

  chat::EnsureChatRegistration();
  chat::win32::EnsureChatPresenterRegistration();
  if (!have_state_dir) {
    std::cerr << "error: --state-dir is required\n";
    PrintUsage();
    return 1;
  }
  if (host && client) {
    std::cerr << "error: --host and --client cannot be used together\n";
    return 1;
  }

  bool const existing =
      chat::HasChatApplicationState(state_dir);
  chat::ChatCreateOptions create;
  if (!existing) {
    create.role = client ? chat::ChatRole::Client
                         : chat::ChatRole::Host;
    if (create.role == chat::ChatRole::Client && !have_name) {
      std::cerr << "error: --client requires --name for a new state\n";
      return 1;
    }
    create.display_name = have_name
                              ? display_name
                              : std::string{chat::kDefaultHostName};
  }

  chat::win32::WinChatApp app;
  return app.Run(state_dir, std::move(create));
}
