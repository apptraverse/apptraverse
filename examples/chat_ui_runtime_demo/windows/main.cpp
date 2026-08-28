#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include "aether/clock.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"

#include "chat_bootstrap.h"
#include "chat_ids.h"
#include "chat_model.h"
#include "win_app.h"
#include "win_presenters.h"

namespace {

std::string DefaultHostName() {
  wchar_t wide[256]{};
  DWORD n = static_cast<DWORD>(std::size(wide));
  if (GetUserNameW(wide, &n) && n > 1) {
    int const bytes =
        WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(n - 1), nullptr,
                            0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(n - 1), out.data(),
                        bytes, nullptr, nullptr);
    if (!out.empty()) {
      return out;
    }
  }
  return apptraverse::chat::kDefaultHostName;
}

void PrintUsage() {
  std::cerr
      << "usage:\n"
      << "  win32_chat_ui_runtime_demo.exe --distill [dir] [--name NAME]\n"
      << "  win32_chat_ui_runtime_demo.exe --host --state-dir <dir>\n"
      << "  win32_chat_ui_runtime_demo.exe --client --state-dir <dir>\n";
}

void DistillChat(std::filesystem::path const& dir, std::string host_name) {
  apptraverse::EnsureChatRegistration();
  apptraverse::EnsureChatPresenterRegistration();
  std::filesystem::remove_all(dir);
  apptraverse::DirectoryDomainStorage storage{dir};
  ae::Domain domain{ae::Now(), storage};
  auto application =
      apptraverse::BuildChatGraph(domain, std::move(host_name));
  apptraverse::FinalizeDistilledGraph(*application);
  // JoinEvent is committed only after runtime Aether UID is known.
  apptraverse::SaveDistilledRoot(*application);  // runtime-save-ok: distill
  auto presentation =
      apptraverse::BuildPresentationGraph(domain, *application);
  apptraverse::SaveDistilledRoot(*presentation);  // runtime-save-ok: distill
}

}  // namespace

int main(int argc, char** argv) {
  bool distill = false;
  bool host = false;
  bool client = false;
  std::filesystem::path state_dir{"chat_ui_runtime_state"};
  std::string host_name = DefaultHostName();
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--distill") {
      distill = true;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        state_dir = argv[++i];
      }
    } else if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    } else if (arg == "--name" && i + 1 < argc) {
      host_name = argv[++i];
    } else if (arg == "--host") {
      host = true;
    } else if (arg == "--client") {
      client = true;
    }
  }

  apptraverse::EnsureChatRegistration();
  apptraverse::EnsureChatPresenterRegistration();
  if (distill) {
    DistillChat(state_dir, std::move(host_name));
    return 0;
  }
  if (host && client) {
    std::cerr << "error: --host and --client cannot be used together\n";
    return 1;
  }
  if (!host && !client) {
    PrintUsage();
    return 1;
  }
  if (!std::filesystem::exists(state_dir)) {
    std::cerr << "distilled state missing: " << state_dir.string()
              << "\nrun with --distill <dir>\n";
    return 1;
  }
  apptraverse::ChatRole const role =
      host ? apptraverse::ChatRole::Host : apptraverse::ChatRole::Client;
  apptraverse::WinChatApp app;
  return app.Run(state_dir, role);
}
