#pragma once
#include <oleacc.h>
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif
#include <vector>
#include "apptraverse/ui/ui_native_presenter_schema.h"
#include "apptraverse/ui/ui_text_control.h"
namespace apptraverse::ui::windows {
class WinTextControlPresenter : public UiTextControlPresenter {
  APPTRAVERSE_NAMED_OBJECT(::apptraverse::ui::native_schema::kText,
                           WinTextControlPresenter, UiTextControlPresenter, 0)
 protected:
  WinTextControlPresenter() = default;

 public:
  explicit WinTextControlPresenter(ae::ObjProp p) : UiTextControlPresenter{p} {}
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
  bool OnCommand(std::uint32_t, std::uint16_t) override;
  HWND hwnd() const { return hwnd_; }

 private:
  static LRESULT CALLBACK Subclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR,
                                   DWORD_PTR);
  HWND hwnd_{};
  IAccPropServices* accessibility_{};
  bool com_initialized_{};
  std::string applied_accessible_label_;
  bool applying_{};
  std::vector<std::string> shown_rows_;
};
}  // namespace apptraverse::ui::windows
