#ifndef APPTRAVERSE_EXAMPLES_ANDROID_LOG_H_
#define APPTRAVERSE_EXAMPLES_ANDROID_LOG_H_

#include <android/log.h>

#include <cstdio>
#include <iostream>
#include <streambuf>
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

// Route Aether IoStreamTrap (std::cout / std::cerr) into logcat.
class AndroidLogStreambuf final : public std::streambuf {
 public:
  explicit AndroidLogStreambuf(int priority) : priority_{priority} {}

 protected:
  int overflow(int ch) override {
    if (ch == traits_type::eof()) {
      return traits_type::not_eof(ch);
    }
    char const c = static_cast<char>(ch);
    if (c == '\n') {
      FlushLine();
    } else if (c != '\r') {
      line_.push_back(c);
      if (line_.size() > 1800) {
        FlushLine();
      }
    }
    return ch;
  }

  int sync() override {
    FlushLine();
    return 0;
  }

 private:
  void FlushLine() {
    if (line_.empty()) {
      return;
    }
    __android_log_write(priority_, "AetherTele", line_.c_str());
    line_.clear();
  }

  int priority_;
  std::string line_;
};

inline void InstallAetherTeleToLogcat() {
  static AndroidLogStreambuf out_buf{ANDROID_LOG_INFO};
  static AndroidLogStreambuf err_buf{ANDROID_LOG_ERROR};
  static bool installed = false;
  if (installed) {
    return;
  }
  std::cout.rdbuf(&out_buf);
  std::cerr.rdbuf(&err_buf);
  installed = true;
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
