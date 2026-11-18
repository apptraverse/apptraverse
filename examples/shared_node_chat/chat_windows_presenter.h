#pragma once

#include "apptraverse/platform/windows/win_ui_presenters.h"

namespace apptraverse::chat {

class ChatDesktopWindowPresenter : public ui::windows::WinDesktopWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::ChatDesktopWindowPresenter",
                           ChatDesktopWindowPresenter,
                           ui::windows::WinDesktopWindowPresenter, 0)
 protected:
  ChatDesktopWindowPresenter() = default;
 public:
  explicit ChatDesktopWindowPresenter(ae::ObjProp prop)
      : WinDesktopWindowPresenter{prop} {}
  template <typename D> void Load(ae::Version<0>, D& d) { d(base_); }
  template <typename D> void Save(ae::Version<0>, D& d) const { d(base_); }

  HMENU CreatePresentationMenu() override;
  void OnLoad() override;
  void OnModelChanged() override;
  bool OnCommand(std::uint32_t id, std::uint16_t notification) override;

 private:
  static constexpr UINT kThemeFirst = 5100;
  void SyncThemeMenu();
  std::optional<ui::UiAppearancePreferences> applied_appearance_;
};

void EnsureChatWindowsPresenterRegistration();

}  // namespace apptraverse::chat
