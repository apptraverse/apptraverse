#ifndef APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_LIST_PRESENTER_H_
#define APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_LIST_PRESENTER_H_

#include <string>
#include <vector>

#include <gtk/gtk.h>

#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::linux {

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
  struct NativeRow {
    ae::ObjId row_id{};
    GtkWidget* row{nullptr};
    GtkWidget* label{nullptr};
    std::string text;
  };

  static NativeRow CreateNativeRow(ae::ObjId row_id, std::string const& label);
  void SyncFromMirror();
  void HandleRowSelected(GtkListBoxRow* row);
  static void OnRowSelected(GtkListBox* box, GtkListBoxRow* row, gpointer user_data);
  static void OnRowActivated(GtkListBox* box, GtkListBoxRow* row,
                             gpointer user_data);

  GtkWidget* list_box_{nullptr};
  std::vector<NativeRow> native_rows_;
  gulong row_selected_handler_{0};
  gulong row_activated_handler_{0};
  bool applying_mirror_sync_{false};
};

}  // namespace apptraverse::ui::linux

#endif  // APPTRAVERSE_PLATFORM_LINUX_LINUX_UI_LIST_PRESENTER_H_
