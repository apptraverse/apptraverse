#include <iostream>

#include "apptraverse/noninteractive_crt.h"
#include "chat_launch_options.h"
#include "linux_chat_app.h"

int main(int argc, char** argv) {
  apptraverse::EnableNoninteractiveCrt();

  auto const parse_res = apptraverse::example::chat_demo::ParseChatLaunchOptions(argc, argv);
  if (!parse_res.ok) {
    std::cerr << parse_res.error_message << '\n';
    return 1;
  }

  apptraverse::example::chat_demo::LinuxChatApp app;
  return app.Run(parse_res.options);
}
