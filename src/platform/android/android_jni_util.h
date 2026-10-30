#ifndef APPTRAVERSE_PLATFORM_ANDROID_ANDROID_JNI_UTIL_H_
#define APPTRAVERSE_PLATFORM_ANDROID_ANDROID_JNI_UTIL_H_

#include <jni.h>

#include <string>

#include "apptraverse/platform/android/android_presentation_host.h"

namespace apptraverse::ui::android {

JNIEnv* RequireUiThreadEnv(AndroidPresentationHost const& host);
void ClearGlobalRef(AndroidPresentationHost const& host, jobject& ref);

std::string JstringToUtf8(JNIEnv* env, jstring value);
jstring Utf8ToJstring(JNIEnv* env, char const* utf8);

}  // namespace apptraverse::ui::android

#endif  // APPTRAVERSE_PLATFORM_ANDROID_ANDROID_JNI_UTIL_H_
