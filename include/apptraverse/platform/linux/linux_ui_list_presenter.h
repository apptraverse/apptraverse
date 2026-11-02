#ifndef APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_LIST_PRESENTER_H_
#define APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_LIST_PRESENTER_H_

#include <gtk/gtk.h>

#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::linux {

class LinuxDesktopWindowPresenter;

class LinuxListContainerPresenter : public UiListContainerPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::ui::linux::LinuxListContainerPresenter",
      LinuxListContainerPresenter, UiListContainerPresenter, 0)

 protected:
  LinuxListContainerPresenter() = default;

 public:
  explicit LinuxListContainerPresenter(ae::ObjProp prop)
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

  GtkWidget* ListWidget() const { return list_box_; }

 private:
  void SyncFromMirror();
  void HandleRowSelected(GtkListBoxRow* row);
  static void OnRowSelected(GtkListBox* box, GtkListBoxRow* row, gpointer user_data);
  LinuxDesktopWindowPresenter* Desktop() const;

  GtkWidget* list_box_{nullptr};
  gulong row_selected_handler_{0};
  bool applying_mirror_sync_{false};
};

}  // namespace apptraverse::ui::linux

#endif  // APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_LIST_PRESENTER_H_
