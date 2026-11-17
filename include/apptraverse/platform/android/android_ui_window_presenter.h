#ifndef APPTRAVERSE_PLATFORM_ANDROID_ANDROID_UI_WINDOW_PRESENTER_H_
#define APPTRAVERSE_PLATFORM_ANDROID_ANDROID_UI_WINDOW_PRESENTER_H_

#include "apptraverse/ui/ui_native_presenter_schema.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_device_state.h"
#if defined(__ANDROID__)
#include <jni.h>
#endif

namespace apptraverse::ui::android {

struct AndroidDesktopWindowViewRefs;
struct AndroidPresentationHost;

class AndroidDesktopWindowPresenter : public UiDesktopWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      ::apptraverse::ui::native_schema::kWindow,
      AndroidDesktopWindowPresenter, UiDesktopWindowPresenter, 0)

 protected:
  AndroidDesktopWindowPresenter() = default;

 public:
  explicit AndroidDesktopWindowPresenter(ae::ObjProp prop)
      : UiDesktopWindowPresenter{prop} {}

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_);
  }

  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;

  void RefreshLocalLayout(std::int32_t width, std::int32_t height, std::int32_t dpi,
                          bool require_complete_bindings = false);
  UiControlLayout ResolveChildLayout(UiControlLayout requested) const override;

 private:
  friend struct AndroidPresentationHost;

  std::vector<UiControlLayout> local_rectangles_;
  std::int32_t client_width_{0}, client_height_{0}, density_dpi_{160};
  AndroidDesktopWindowViewRefs* view_refs_{nullptr};
};

#if defined(__ANDROID__)
UiDeviceEnvironment CaptureAndroidDeviceEnvironment(JNIEnv* env, jobject activity);
#endif

void NotifyAndroidWindowClientSizeFromJava(std::int64_t native_presenter,
                                         std::int32_t width,
                                         std::int32_t height,
                                         std::int32_t density_dpi);

}  // namespace apptraverse::ui::android

#endif  // APPTRAVERSE_PLATFORM_ANDROID_ANDROID_UI_WINDOW_PRESENTER_H_
