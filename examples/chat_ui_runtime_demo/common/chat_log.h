#ifndef APPTRAVERSE_CHAT_LOG_H_
#define APPTRAVERSE_CHAT_LOG_H_

#include <fstream>
#include <iostream>
#include <mutex>
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
namespace detail {

inline std::mutex& LogMutex() {
  static std::mutex mu;
  return mu;
}

inline std::string& LogPath() {
  static std::string path;
  return path;
}

}  // namespace detail

inline void SetChatLogPath(std::string path) {
  std::lock_guard<std::mutex> lock{detail::LogMutex()};
  detail::LogPath() = std::move(path);
}

inline void ChatLog(std::string const& line) {
  {
    std::lock_guard<std::mutex> lock{detail::LogMutex()};
    std::cout << line << '\n';
    std::fflush(stdout);
    if (!detail::LogPath().empty()) {
      std::ofstream out{detail::LogPath(), std::ios::out | std::ios::app};
      if (out) {
        out << line << '\n';
      }
    }
  }
#ifdef _WIN32
  OutputDebugStringA((line + "\n").c_str());
#endif
}

}  // namespace apptraverse::chat

#endif  // APPTRAVERSE_CHAT_LOG_H_
