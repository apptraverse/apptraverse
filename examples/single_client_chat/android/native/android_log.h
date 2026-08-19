#ifndef APPTRAVERSE_EXAMPLES_ANDROID_LOG_H_
#define APPTRAVERSE_EXAMPLES_ANDROID_LOG_H_

#include <android/log.h>
#include <sys/system_properties.h>

#include <cctype>
#include <cstdio>
#include <iostream>
#include <streambuf>
#include <string>

namespace apptraverse::android {

inline constexpr char const kLogTag[] = "App TraverseChat";
inline constexpr char const kVerboseProperty[] = "debug.apptraverse.verbose_log";

inline bool EnvFlagIsTrue(char const* raw) {
  if (raw == nullptr || raw[0] == '\0') {
    return false;
  }
  std::string value{raw};
  while (!value.empty() &&
         (value.front() == ' ' || value.front() == '\t')) {
    value.erase(value.begin());
  }
  while (!value.empty() &&
         (value.back() == ' ' || value.back() == '\t' ||
          value.back() == '\r' || value.back() == '\n')) {
    value.pop_back();
  }
  for (char& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value == "1" || value == "true" || value == "yes" || value == "on";
}

// Read debug.apptraverse.verbose_log once per native process.
inline bool VerboseLogEnabled() {
  static bool const enabled = [] {
    char value[PROP_VALUE_MAX] = {};
    __system_property_get(kVerboseProperty, value);
    return EnvFlagIsTrue(value);
  }();
  return enabled;
}

inline void LogMarker(std::string const& line) {
  if (!VerboseLogEnabled()) {
    return;
  }
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

class DiscardStreambuf final : public std::streambuf {
 protected:
  int overflow(int ch) override {
    return ch == traits_type::eof() ? traits_type::not_eof(ch) : ch;
  }
  std::streamsize xsputn(char const*, std::streamsize count) override {
    return count;
  }
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

inline void SilenceAetherTeleToNull() {
  static DiscardStreambuf discard;
  std::cout.rdbuf(&discard);
}

inline void ConfigureAetherTeleOutput() {
  if (VerboseLogEnabled()) {
    InstallAetherTeleToLogcat();
  } else {
    SilenceAetherTeleToNull();
  }
}

inline std::string PointerToHex(void const* pointer) {
  char buffer[(sizeof(void*) * 2) + 3] = {};
  std::snprintf(buffer, sizeof(buffer), "%p", pointer);
  return std::string{buffer};
}

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
