#ifndef APPTRAVERSE_EXAMPLES_APPLE_LOG_H_
#define APPTRAVERSE_EXAMPLES_APPLE_LOG_H_

#include <cstdio>
#include <string>

namespace apptraverse::apple {

inline void LogMarker(std::string const& line) {
  std::fputs("[AppTraverse] ", stderr);
  std::fputs(line.c_str(), stderr);
  std::fputc('\n', stderr);
  std::fflush(stderr);
}

inline void LogError(std::string const& line) {
  std::fputs("[AppTraverse ERROR] ", stderr);
  std::fputs(line.c_str(), stderr);
  std::fputc('\n', stderr);
  std::fflush(stderr);
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

}  // namespace apptraverse::apple

#endif  // APPTRAVERSE_EXAMPLES_APPLE_LOG_H_
