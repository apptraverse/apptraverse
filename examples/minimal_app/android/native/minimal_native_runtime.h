#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_NATIVE_RUNTIME_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_NATIVE_RUNTIME_H_

#include <filesystem>
#include <memory>

#include <jni.h>

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/platform/android/android_application_host.h"
#include "apptraverse/platform/android/android_presentation_host.h"
#include "apptraverse/platform/android/android_ui_bridge.h"

namespace apptraverse::example::minimal {

class MinimalNativeRuntime {
 public:
  MinimalNativeRuntime(std::filesystem::path state_dir,
                         android::AndroidUiBridge ui_bridge);
  ~MinimalNativeRuntime();

  MinimalNativeRuntime(MinimalNativeRuntime const&) = delete;
  MinimalNativeRuntime& operator=(MinimalNativeRuntime const&) = delete;

  void SetPresentationRoot(JNIEnv* env, jobject activity, jobject content_root);
  void StartModelThread();
  void ConsumePublication(int kind);

 private:
  std::filesystem::path state_dir_;
  ApplicationModelRuntime runtime_;
  ui::android::AndroidPresentationHost presentation_host_{};
  android::AndroidUiBridge ui_bridge_;
  std::unique_ptr<android::AndroidApplicationHost> host_;
};

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_NATIVE_RUNTIME_H_
