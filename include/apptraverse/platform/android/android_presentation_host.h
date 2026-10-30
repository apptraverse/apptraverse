#ifndef APPTRAVERSE_PLATFORM_ANDROID_ANDROID_PRESENTATION_HOST_H_
#define APPTRAVERSE_PLATFORM_ANDROID_ANDROID_PRESENTATION_HOST_H_

#include <cstdint>

#include <jni.h>

namespace ae {
class Domain;
}

namespace apptraverse::ui::android {

using AndroidResolvePushButtonLabelUtf8Fn = char const* (*)(
    std::uint32_t control_id, ae::Domain& domain);

// Passed as Presenter::presentation_host for Android UI. Not serialized.
struct AndroidPresentationHost {
  JavaVM* vm{nullptr};
  jobject activity{nullptr};
  jobject content_root{nullptr};
  AndroidResolvePushButtonLabelUtf8Fn resolve_push_button_label_utf8{nullptr};
};

}  // namespace apptraverse::ui::android

#endif  // APPTRAVERSE_PLATFORM_ANDROID_ANDROID_PRESENTATION_HOST_H_
