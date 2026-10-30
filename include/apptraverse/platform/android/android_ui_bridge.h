#ifndef APPTRAVERSE_PLATFORM_ANDROID_ANDROID_UI_BRIDGE_H_
#define APPTRAVERSE_PLATFORM_ANDROID_ANDROID_UI_BRIDGE_H_

#include <jni.h>

namespace apptraverse::android {

class AndroidUiBridge {
 public:
  AndroidUiBridge() = default;
  AndroidUiBridge(JavaVM* vm, jobject global_object, jclass global_class,
                  jmethodID on_publication);
  ~AndroidUiBridge();
  AndroidUiBridge(AndroidUiBridge&&) noexcept;
  AndroidUiBridge& operator=(AndroidUiBridge&&) noexcept;

  AndroidUiBridge(AndroidUiBridge const&) = delete;
  AndroidUiBridge& operator=(AndroidUiBridge const&) = delete;

  [[nodiscard]] bool is_valid() const { return vm_ != nullptr && object_ != nullptr; }

  void PostPublication(int kind) const;

 private:
  void Reset();
  JNIEnv* AttachedEnv() const;

  JavaVM* vm_{nullptr};
  jobject object_{nullptr};
  jclass class_{nullptr};
  jmethodID on_publication_{nullptr};
};

AndroidUiBridge MakeAndroidUiBridge(JNIEnv* env, jobject ui_bridge);

}  // namespace apptraverse::android

#endif  // APPTRAVERSE_PLATFORM_ANDROID_ANDROID_UI_BRIDGE_H_
