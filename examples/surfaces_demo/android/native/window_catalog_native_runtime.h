#ifndef APPTRAVERSE_EXAMPLE_SURFACES_WINDOW_CATALOG_NATIVE_RUNTIME_H_
#define APPTRAVERSE_EXAMPLE_SURFACES_WINDOW_CATALOG_NATIVE_RUNTIME_H_

#include <filesystem>
#include <memory>

#include <jni.h>

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/platform/android/android_application_host.h"
#include "apptraverse/platform/android/android_presentation_host.h"
#include "apptraverse/platform/android/android_ui_bridge.h"

#include "../window_catalog_android_embedded_profile.h"
#include "window_catalog_ui_bridge.h"

#include "window_catalog_model.h"

namespace apptraverse::example::surfaces {

class WindowCatalogNativeRuntime {
 public:
  WindowCatalogNativeRuntime(
      std::filesystem::path state_dir,
      android::WindowCatalogUiBridge ui_bridge,
      android::AndroidUiBridge android_ui_bridge);
  ~WindowCatalogNativeRuntime();

  void SetPresentationRoot(JNIEnv* env, jobject activity, jobject content_root);
  void StartModelThread();
  void ConsumePublication(int kind);
  void RequestBackgroundSave();
  void MobilePageShown(std::uint32_t window_obj_id);
  void MobileSwipe(int delta);

 private:
  void PublishMobilePages();
  void SyncMobilePagerChrome(apptraverse::WindowCatalog& catalog);

  std::filesystem::path state_dir_;
  android::WindowCatalogUiBridge ui_bridge_;
  android::AndroidUiBridge android_ui_bridge_;
  WindowCatalogAndroidEmbeddedProfile profile_;
  ApplicationModelRuntime runtime_;
  ui::android::AndroidPresentationHost presentation_host_;
  std::unique_ptr<android::AndroidApplicationHost> host_;
  ae::ObjId reported_active_for_ui_{};
};

}  // namespace apptraverse::example::surfaces

#endif  // APPTRAVERSE_EXAMPLE_SURFACES_WINDOW_CATALOG_NATIVE_RUNTIME_H_
