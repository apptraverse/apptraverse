#ifndef APPTRAVERSE_CHAT_LOG_H_
#define APPTRAVERSE_CHAT_LOG_H_

#include <iostream>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif
#endif

namespace apptraverse::chat {

inline void ChatLog(std::string const& line) {
  std::cout << line << '\n';
  std::fflush(stdout);
#ifdef _WIN32
  OutputDebugStringA((line + "\n").c_str());
#endif
}

}  // namespace apptraverse::chat

#endif  // APPTRAVERSE_CHAT_LOG_H_
