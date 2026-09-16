#ifndef APPTRAVERSE_CHAT_DEMO_ANDROID_LOG_H_
#define APPTRAVERSE_CHAT_DEMO_ANDROID_LOG_H_

#include <android/log.h>

#include <string>

namespace apptraverse::example::chat_demo::android {

inline constexpr char const kLogTag[] = "AppTraverseChat";

inline void LogMarker(std::string const& line) {
  __android_log_write(ANDROID_LOG_INFO, kLogTag, line.c_str());
}

inline void LogError(std::string const& line) {
  __android_log_write(ANDROID_LOG_ERROR, kLogTag, line.c_str());
}

}  // namespace apptraverse::example::chat_demo::android

#endif  // APPTRAVERSE_CHAT_DEMO_ANDROID_LOG_H_
