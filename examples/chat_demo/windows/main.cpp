#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <string>
#include <vector>

#include "apptraverse/noninteractive_crt.h"
#include "chat_launch_options.h"
#include "win_chat_app.h"

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

  auto const parse_res = apptraverse::example::chat_demo::ParseChatLaunchOptions(args);
  if (!parse_res.ok) {
    MessageBoxA(nullptr, parse_res.error_message.c_str(), "AppTraverse Chat - Argument Error", MB_ICONERROR | MB_OK);
    return 1;
  }

  apptraverse::example::chat_demo::WinChatApp app;
  return app.Run(parse_res.options);
}
