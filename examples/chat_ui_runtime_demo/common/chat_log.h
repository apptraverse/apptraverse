#ifndef CHAT_LOG_H_
#define CHAT_LOG_H_

#include <chrono>
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

namespace chat {
namespace detail {

inline std::mutex& LogMutex() {
  static std::mutex mu;
  return mu;
}

inline std::string& LogPath() {
  static std::string path;
  return path;
}

inline std::string& SessionId() {
  static std::string id;
  return id;
}

inline std::string MakeSessionId() {
  auto const now = std::chrono::steady_clock::now().time_since_epoch();
  auto const us =
      std::chrono::duration_cast<std::chrono::microseconds>(now).count();
#ifdef _WIN32
  auto const pid = static_cast<unsigned long>(GetCurrentProcessId());
#else
  auto const pid = 0ul;
#endif
  return std::to_string(pid) + "-" + std::to_string(us);
}

}  // namespace detail

inline std::string const& CurrentSessionId() {
  return detail::SessionId();
}

inline void SetChatLogPath(std::string path) {
  std::lock_guard<std::mutex> lock{detail::LogMutex()};
  detail::LogPath() = std::move(path);
}

inline void ChatLog(std::string const& line) {
  std::string out = line;
  {
    std::lock_guard<std::mutex> lock{detail::LogMutex()};
    if (!detail::SessionId().empty() &&
        line.find("session_id=") == std::string::npos) {
      out += " session_id=";
      out += detail::SessionId();
    }
    std::cout << out << '\n';
    std::fflush(stdout);
    if (!detail::LogPath().empty()) {
      std::ofstream file{detail::LogPath(), std::ios::out | std::ios::app};
      if (file) {
        file << out << '\n';
      }
    }
  }
#ifdef _WIN32
  OutputDebugStringA((out + "\n").c_str());
#endif
}

inline void BeginChatSession() {
  {
    std::lock_guard<std::mutex> lock{detail::LogMutex()};
    detail::SessionId() = detail::MakeSessionId();
  }
  ChatLog("APP_SESSION_START session_id=" + detail::SessionId());
}

}  // namespace chat

#endif  // CHAT_LOG_H_
