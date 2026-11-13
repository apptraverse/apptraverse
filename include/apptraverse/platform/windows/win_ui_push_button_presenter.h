#ifndef APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_PUSH_BUTTON_PRESENTER_H_
#define APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_PUSH_BUTTON_PRESENTER_H_

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "apptraverse/ui/ui_native_presenter_schema.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::windows {

class WinPushButtonPresenter : public UiPushButtonPresenter {
  APPTRAVERSE_NAMED_OBJECT(::apptraverse::ui::native_schema::kPushButton,
                           WinPushButtonPresenter, UiPushButtonPresenter, 0)

 protected:
  WinPushButtonPresenter() : hwnd_{nullptr} {}

 public:
  explicit WinPushButtonPresenter(ae::ObjProp prop)
      : UiPushButtonPresenter{prop}, hwnd_{nullptr} {}

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_); }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_); }

  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;

  bool OnCommand(std::uint32_t command_id,
                 std::uint16_t notification_code) override;

  HWND hwnd() const { return hwnd_; }

 private:
  void SyncFromMirror();
  void ApplyLayout();

  HWND hwnd_;
};

}  // namespace apptraverse::ui::windows

#endif  // APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_PUSH_BUTTON_PRESENTER_H_
