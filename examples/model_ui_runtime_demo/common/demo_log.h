#ifndef APPTRAVERSE_DEMO_LOG_H_
#define APPTRAVERSE_DEMO_LOG_H_

#include <iostream>
#include <string>

namespace apptraverse::demo {

inline void DemoLog(std::string const& line) {
  std::cout << line << '\n';
  std::fflush(stdout);
}

}  // namespace apptraverse::demo

#endif  // APPTRAVERSE_DEMO_LOG_H_
