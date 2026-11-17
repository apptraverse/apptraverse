#pragma once
#include <gtk/gtk.h>
#include "apptraverse/ui/ui_native_presenter_schema.h"
#include "apptraverse/ui/ui_text_control.h"
namespace apptraverse::ui::linux {
class LinuxTextControlPresenter : public UiTextControlPresenter {
  APPTRAVERSE_NAMED_OBJECT(::apptraverse::ui::native_schema::kText,
                           LinuxTextControlPresenter, UiTextControlPresenter, 0)
 protected:
  LinuxTextControlPresenter() = default;

 public:
  explicit LinuxTextControlPresenter(ae::ObjProp p)
      : UiTextControlPresenter{p} {}
  template <class D>
  void Load(ae::Version<0>, D& d) {
    d(base_);
  }
  template <class D>
  void Save(ae::Version<0>, D& d) const {
    d(base_);
  }
  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;
  GtkWidget* NativeWidget() const { return input_; }

 private:
  GtkWidget* widget_{};
  GtkWidget* input_{};
  bool applying_{};
  void Sync();
  void Edited();
  static void OnChanged(GtkWidget*, gpointer);
  static void OnBufferChanged(GtkTextBuffer*, gpointer);
  static void OnAccept(GtkEntry*, gpointer);
};
}  // namespace apptraverse::ui::linux
