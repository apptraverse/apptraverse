#ifndef APPTRAVERSE_PLATFORM_ANDROID_ANDROID_LOG_H_
#define APPTRAVERSE_PLATFORM_ANDROID_ANDROID_LOG_H_

#include <android/log.h>

namespace apptraverse::android {

inline void LogInfo(char const* message) {
  __android_log_print(ANDROID_LOG_INFO, "AppTraverse", "%s", message);
}

inline void LogError(char const* message) {
  __android_log_print(ANDROID_LOG_ERROR, "AppTraverse", "%s", message);
}

}  // namespace apptraverse::android

#endif  // APPTRAVERSE_PLATFORM_ANDROID_ANDROID_LOG_H_
