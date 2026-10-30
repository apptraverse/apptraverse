#ifndef APPTRAVERSE_PLATFORM_ANDROID_ANDROID_PRESENTATION_HOST_H_
#define APPTRAVERSE_PLATFORM_ANDROID_ANDROID_PRESENTATION_HOST_H_

#include <jni.h>

namespace apptraverse::ui::android {

// Passed as Presenter::presentation_host for Android UI. Not serialized.
struct AndroidPresentationHost {
  JavaVM* vm{nullptr};
  jobject activity{nullptr};
  jobject content_root{nullptr};
};

}  // namespace apptraverse::ui::android

#endif  // APPTRAVERSE_PLATFORM_ANDROID_ANDROID_PRESENTATION_HOST_H_
