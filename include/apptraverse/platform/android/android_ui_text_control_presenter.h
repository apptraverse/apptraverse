#pragma once
#include "apptraverse/ui/ui_native_presenter_schema.h"
#include "apptraverse/ui/ui_text_control.h"
namespace apptraverse::ui::android {
class AndroidTextControlPresenter : public UiTextControlPresenter {
  APPTRAVERSE_NAMED_OBJECT(::apptraverse::ui::native_schema::kText,
                           AndroidTextControlPresenter, UiTextControlPresenter,
                           0)
 protected:
  AndroidTextControlPresenter() = default;

 public:
  explicit AndroidTextControlPresenter(ae::ObjProp p)
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

 private:
  friend struct AndroidPresentationHost;
  struct ViewRefs;
  ViewRefs* view_refs_{};
};
}  // namespace apptraverse::ui::android
