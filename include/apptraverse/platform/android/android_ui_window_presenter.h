#ifndef APPTRAVERSE_PLATFORM_ANDROID_ANDROID_UI_WINDOW_PRESENTER_H_
#define APPTRAVERSE_PLATFORM_ANDROID_ANDROID_UI_WINDOW_PRESENTER_H_

#include "apptraverse/ui/ui_native_presenter_schema.h"
#include "apptraverse/ui/ui_presenters.h"

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

  UiControlLayout ResolveChildLayout(UiControlLayout requested) const override;

 private:
  friend struct AndroidPresentationHost;

  AndroidDesktopWindowViewRefs* view_refs_{nullptr};
};

void NotifyAndroidWindowClientSizeFromJava(std::int64_t native_presenter,
                                         std::int32_t width,
                                         std::int32_t height,
                                         std::int32_t density_dpi);

}  // namespace apptraverse::ui::android

#endif  // APPTRAVERSE_PLATFORM_ANDROID_ANDROID_UI_WINDOW_PRESENTER_H_
