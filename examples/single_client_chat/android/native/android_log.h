#ifndef APPTRAVERSE_EXAMPLES_ANDROID_LOG_H_
#define APPTRAVERSE_EXAMPLES_ANDROID_LOG_H_

#include <android/log.h>

#include <cstdio>
#include <string>

namespace apptraverse::android {

inline constexpr char const kLogTag[] = "AppTraverseChat";

// Markers are consumed by tools/android/run_single_client_chat_smoke.ps1.
// They go to logcat and to stdout so a plain adb shell run also shows them.
inline void LogMarker(std::string const& line) {
  __android_log_write(ANDROID_LOG_INFO, kLogTag, line.c_str());
  std::fputs(line.c_str(), stdout);
  std::fputc('\n', stdout);
  std::fflush(stdout);
}

inline void LogError(std::string const& line) {
  __android_log_write(ANDROID_LOG_ERROR, kLogTag, line.c_str());
  std::fputs(line.c_str(), stderr);
  std::fputc('\n', stderr);
  std::fflush(stderr);
}

inline std::string PointerToHex(void const* pointer) {
  char buffer[(sizeof(void*) * 2) + 3] = {};
  std::snprintf(buffer, sizeof(buffer), "%p", pointer);
  return std::string{buffer};
}

// Keeps a marker on a single logcat line.
inline std::string ToSingleLine(std::string const& text) {
  std::string line;
  line.reserve(text.size());
  for (char const symbol : text) {
    if (symbol == '\n') {
      line += " | ";
    } else if (symbol != '\r') {
      line.push_back(symbol);
    }
  }
  return line;
}

}  // namespace apptraverse::android

#endif  // APPTRAVERSE_EXAMPLES_ANDROID_LOG_H_
