#ifndef APPTRAVERSE_SURFACES_ANDROID_LOG_H_
#define APPTRAVERSE_SURFACES_ANDROID_LOG_H_

#include <android/log.h>

#include <string>

namespace apptraverse::android {

inline constexpr char const kLogTag[] = "AppTraverseSurfaces";

// Markers are consumed by tools/android/run_surfaces_smoke.ps1.
inline void LogMarker(std::string const& line) {
  __android_log_write(ANDROID_LOG_INFO, kLogTag, line.c_str());
}

inline void LogError(std::string const& line) {
  __android_log_write(ANDROID_LOG_ERROR, kLogTag, line.c_str());
}

}  // namespace apptraverse::android

#endif  // APPTRAVERSE_SURFACES_ANDROID_LOG_H_
