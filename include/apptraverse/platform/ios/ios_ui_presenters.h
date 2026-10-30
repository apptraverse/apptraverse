#ifndef APPTRAVERSE_PLATFORM_IOS_IOS_UI_PRESENTERS_H_
#define APPTRAVERSE_PLATFORM_IOS_IOS_UI_PRESENTERS_H_

#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_push_button_presenter.h"

namespace apptraverse::ui::ios {

class IosDesktopWindowPresenter : public UiDesktopWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::ios::IosDesktopWindowPresenter",
                           IosDesktopWindowPresenter, UiDesktopWindowPresenter,
                           0)

 protected:
  IosDesktopWindowPresenter() = default;

 public:
  explicit IosDesktopWindowPresenter(ae::ObjProp prop)
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
};

class IosPushButtonPresenter : public UiPushButtonPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::ios::IosPushButtonPresenter",
                           IosPushButtonPresenter, UiPushButtonPresenter, 0)

 protected:
  IosPushButtonPresenter() = default;

 public:
  explicit IosPushButtonPresenter(ae::ObjProp prop)
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

class IosListContainerPresenter : public UiListContainerPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::ios::IosListContainerPresenter",
                           IosListContainerPresenter, UiListContainerPresenter,
                           0)

 protected:
  IosListContainerPresenter() = default;

 public:
  explicit IosListContainerPresenter(ae::ObjProp prop)
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

void EnsureIosUiPresenterRegistration();

}  // namespace apptraverse::ui::ios

#endif  // APPTRAVERSE_PLATFORM_IOS_IOS_UI_PRESENTERS_H_
