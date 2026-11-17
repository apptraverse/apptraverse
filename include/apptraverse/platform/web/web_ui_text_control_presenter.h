#pragma once
#include <emscripten/val.h>
#include "apptraverse/ui/ui_native_presenter_schema.h"
#include "apptraverse/ui/ui_text_control.h"
namespace apptraverse::ui::web {
class WebTextControlPresenter : public UiTextControlPresenter {
  APPTRAVERSE_NAMED_OBJECT(::apptraverse::ui::native_schema::kText,
                           WebTextControlPresenter, UiTextControlPresenter, 0)
 protected:
  WebTextControlPresenter() = default;

 public:
  explicit WebTextControlPresenter(ae::ObjProp p) : UiTextControlPresenter{p} {}
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
  emscripten::EM_VAL element_{nullptr};
  void Sync();
};
}  // namespace apptraverse::ui::web
