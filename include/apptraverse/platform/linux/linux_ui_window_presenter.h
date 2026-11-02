#ifndef APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_WINDOW_PRESENTER_H_
#define APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_WINDOW_PRESENTER_H_

#include <gtk/gtk.h>

#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::linux {

class LinuxDesktopWindowPresenter : public UiDesktopWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::ui::linux::LinuxDesktopWindowPresenter",
      LinuxDesktopWindowPresenter, UiDesktopWindowPresenter, 0)

 protected:
  LinuxDesktopWindowPresenter() = default;

 public:
  explicit LinuxDesktopWindowPresenter(ae::ObjProp prop)
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

  void* NativeChildParentHandle() const override;

  GtkWidget* ClientMount() const;
  GtkWidget* TopLevelWindow() const { return window_; }

 private:
  std::string ResolveTitleText() const;
  void SyncTitleFromMirror();
  static gboolean OnDeleteEvent(GtkWidget* widget, GdkEvent* event,
                                gpointer user_data);

  GtkWidget* window_{nullptr};
  GtkWidget* client_mount_{nullptr};
  gulong delete_handler_{0};
};

}  // namespace apptraverse::ui::linux

#endif  // APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_WINDOW_PRESENTER_H_
