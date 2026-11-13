#ifndef APPTRAVERSE_PLATFORM_ANDROID_ANDROID_JNI_UTIL_H_
#define APPTRAVERSE_PLATFORM_ANDROID_ANDROID_JNI_UTIL_H_

#include <jni.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "apptraverse/platform/android/android_presentation_host.h"

namespace apptraverse::ui::android {

JNIEnv* RequireUiThreadEnv(AndroidPresentationHost const& host);
void ClearGlobalRef(AndroidPresentationHost const& host, jobject& ref);

[[noreturn]] void FailJni(JNIEnv* env, char const* operation);
// Native creation/dispatch is an all-or-nothing boundary. Never continue with
// a pending Java exception or a missing mandatory class/method/view handle.
template <class Call>
auto CheckedJni(JNIEnv* env, char const* operation, Call&& call) {
  if constexpr (std::is_void_v<decltype(call())>) {
    std::forward<Call>(call)();
    if (env->ExceptionCheck()) FailJni(env, operation);
  } else {
    auto result = std::forward<Call>(call)();
    if (env->ExceptionCheck()) FailJni(env, operation);
    if constexpr (std::is_pointer_v<decltype(result)>) {
      if (result == nullptr) FailJni(env, operation);
    }
    return result;
  }
}

std::string JstringToUtf8(JNIEnv* env, jstring value);
jstring Utf8ToJstring(JNIEnv* env, char const* utf8);
jstring Utf8ToJstring(JNIEnv* env, std::string_view utf8);

struct ViewContentSize {
  std::int32_t width{0};
  std::int32_t height{0};
};

ViewContentSize ReadViewContentSize(JNIEnv* env, jobject view);

// DisplayMetrics.densityDpi from the Activity context. JNI failure is fatal.
std::int32_t ReadActivityDensityDpi(JNIEnv* env, jobject activity);

}  // namespace apptraverse::ui::android

#endif  // APPTRAVERSE_PLATFORM_ANDROID_ANDROID_JNI_UTIL_H_
