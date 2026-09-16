#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "apptraverse/noninteractive_crt.h"
#include "chat_build_info.h"
#include "chat_launch_options.h"
#include "win_chat_app.h"

namespace {

bool WriteUtf8File(std::string const& path, std::string const& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  return static_cast<bool>(out);
}

// Consumes --dump-parsed-launch <path> before ParseChatLaunchOptions so the
// parser stays strict about unknown tokens. Does not weaken parse rules.
bool ConsumeDumpParsedLaunchFlag(std::vector<std::string>& args,
                                 std::optional<std::string>* dump_path,
                                 std::string* error) {
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] != "--dump-parsed-launch") {
      continue;
    }
    if (i + 1 >= args.size()) {
      *error = "Missing value for --dump-parsed-launch";
      return false;
    }
    *dump_path = args[i + 1];
    args.erase(args.begin() + static_cast<std::ptrdiff_t>(i),
               args.begin() + static_cast<std::ptrdiff_t>(i + 2));
    return true;
  }
  return true;
}

bool WriteParsedLaunchDump(
    std::string const& path,
    apptraverse::example::chat_demo::ChatLaunchOptions const& options) {
  using apptraverse::example::chat_demo::DefaultChatExampleStateDir;
  using apptraverse::example::chat_demo::DemoRole;
  std::filesystem::path state_dir;
  if (options.state_dir.has_value()) {
    state_dir = *options.state_dir;
  } else {
    state_dir = DefaultChatExampleStateDir(options.role);
  }
  std::string text;
  text += "role=";
  text += options.role == DemoRole::kHost ? "host" : "client";
  text += '\n';
  text += "state_dir=";
  text += state_dir.string();
  text += '\n';
  return WriteUtf8File(path, text);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE /*instance*/, HINSTANCE /*prev_instance*/,
                    PWSTR /*cmd_line*/, int /*show_cmd*/) {
  apptraverse::EnableNoninteractiveCrt();

  int argc = 0;
  LPWSTR* argv_w = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::vector<std::string> args;
  if (argv_w != nullptr) {
    for (int i = 1; i < argc; ++i) {
      int const len = WideCharToMultiByte(CP_UTF8, 0, argv_w[i], -1, nullptr, 0, nullptr, nullptr);
      if (len > 0) {
        std::string utf8(static_cast<std::size_t>(len), '\0');
        WideCharToMultiByte(CP_UTF8, 0, argv_w[i], -1, utf8.data(), len, nullptr, nullptr);
        if (!utf8.empty() && utf8.back() == '\0') {
          utf8.pop_back();
        }
        args.push_back(std::move(utf8));
      }
    }
    LocalFree(argv_w);
  }

  // Offline identity only — before profile lock, windows, or Aether.
  int build_info_exit = 0;
  if (apptraverse::example::chat_demo::TryHandleBuildInfoArgs(args,
                                                              &build_info_exit)) {
    return build_info_exit;
  }

  std::optional<std::string> dump_parsed_launch;
  std::string dump_flag_error;
  if (!ConsumeDumpParsedLaunchFlag(args, &dump_parsed_launch, &dump_flag_error)) {
    MessageBoxA(nullptr, dump_flag_error.c_str(),
                "AppTraverse Chat - Argument Error", MB_ICONERROR | MB_OK);
    return 1;
  }

  auto const parse_res = apptraverse::example::chat_demo::ParseChatLaunchOptions(args);
  if (!parse_res.ok) {
    MessageBoxA(nullptr, parse_res.error_message.c_str(), "AppTraverse Chat - Argument Error", MB_ICONERROR | MB_OK);
    return 1;
  }
  if (parse_res.options.show_help) {
    MessageBoxA(nullptr,
                apptraverse::example::chat_demo::ChatLaunchUsageText().c_str(),
                "App Traverse Chat", MB_OK);
    return 0;
  }

  if (dump_parsed_launch.has_value()) {
    if (!WriteParsedLaunchDump(*dump_parsed_launch, parse_res.options)) {
      MessageBoxA(nullptr, "Failed to write --dump-parsed-launch file",
                  "AppTraverse Chat - Argument Error", MB_ICONERROR | MB_OK);
      return 1;
    }
    return 0;
  }

  apptraverse::example::chat_demo::WinChatApp app;
  return app.Run(parse_res.options);
}
