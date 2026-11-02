#ifndef APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_PUSH_BUTTON_PRESENTER_H_
#define APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_PUSH_BUTTON_PRESENTER_H_

#include <gtk/gtk.h>

#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::linux {

class LinuxPushButtonPresenter : public UiPushButtonPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::ui::linux::LinuxPushButtonPresenter",
      LinuxPushButtonPresenter, UiPushButtonPresenter, 0)

 protected:
  LinuxPushButtonPresenter() = default;

 public:
  explicit LinuxPushButtonPresenter(ae::ObjProp prop)
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

 private:
  void SyncFromMirror();

  GtkWidget* button_{nullptr};
};

}  // namespace apptraverse::ui::linux

#endif  // APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_PUSH_BUTTON_PRESENTER_H_
