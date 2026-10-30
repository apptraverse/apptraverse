#ifndef APPTRAVERSE_PLATFORM_MACOS_MAC_UI_PRESENTERS_H_
#define APPTRAVERSE_PLATFORM_MACOS_MAC_UI_PRESENTERS_H_

#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_push_button_presenter.h"

namespace apptraverse::ui::macos {

class MacDesktopWindowPresenter : public UiDesktopWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::ui::macos::MacDesktopWindowPresenter",
      MacDesktopWindowPresenter, UiDesktopWindowPresenter, 0)

 protected:
  MacDesktopWindowPresenter()
      : native_window_{nullptr},
        window_delegate_{nullptr},
        applying_native_frame_{false} {}

 public:
  explicit MacDesktopWindowPresenter(ae::ObjProp prop)
      : UiDesktopWindowPresenter{prop},
        native_window_{nullptr},
        window_delegate_{nullptr},
        applying_native_frame_{false} {}

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
  void ApplyRequestedFrameFromMirror() override;

  void NotifyUserClose();
  void ReportNativeFrame();
  void* native_window() const { return native_window_; }

 private:
  void* native_window_;
  void* window_delegate_;
  bool applying_native_frame_;
};

class MacPushButtonPresenter : public UiPushButtonPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::macos::MacPushButtonPresenter",
                           MacPushButtonPresenter, UiPushButtonPresenter, 0)

 protected:
  MacPushButtonPresenter() = default;

 public:
  explicit MacPushButtonPresenter(ae::ObjProp prop)
      : UiPushButtonPresenter{prop} {}

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
};

class MacListContainerPresenter : public UiListContainerPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::ui::macos::MacListContainerPresenter",
      MacListContainerPresenter, UiListContainerPresenter, 0)

 protected:
  MacListContainerPresenter() = default;

 public:
  explicit MacListContainerPresenter(ae::ObjProp prop)
      : UiListContainerPresenter{prop} {}

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
};

void EnsureMacUiPresenterRegistration();

}  // namespace apptraverse::ui::macos

#endif  // APPTRAVERSE_PLATFORM_MACOS_MAC_UI_PRESENTERS_H_
