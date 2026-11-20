#pragma once

#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/platform/windows/win_ui_text_control_presenter.h"

namespace apptraverse::chat {

class SetChatWindowMaximized;

class ChatWindowState : public NodeFor<ChatWindowState, ClientLocalState> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::ChatWindowState",
                           ChatWindowState, ClientLocalState, 0)
 protected:
  ChatWindowState() = default;

 public:
  explicit ChatWindowState(ae::ObjProp prop) : NodeFor{prop} {}
  bool maximized{};
  AE_OBJECT_REFLECT(AE_MMBR(maximized))
  template <typename D>
  void Load(ae::Version<0>, D& d) {
    d(base_, maximized);
  }
  template <typename D>
  void Save(ae::Version<0>, D& d) const {
    d(base_, maximized);
  }
  void SetMaximized(bool value);
  void Apply(SetChatWindowMaximized const& event);
};

class SetChatWindowMaximized
    : public EventFor<ChatWindowState, SetChatWindowMaximized> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::SetChatWindowMaximized",
                           SetChatWindowMaximized, Event, 0)
 protected:
  SetChatWindowMaximized() = default;

 public:
  explicit SetChatWindowMaximized(ae::ObjProp prop) : EventFor{prop} {}
  bool value{};
  AE_OBJECT_REFLECT(AE_MMBR(value))
};

class ChatDesktopWindowPresenter : public ui::windows::WinDesktopWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::ChatDesktopWindowPresenter",
                           ChatDesktopWindowPresenter,
                           ui::windows::WinDesktopWindowPresenter, 0)
 protected:
  ChatDesktopWindowPresenter() = default;
 public:
  explicit ChatDesktopWindowPresenter(ae::ObjProp prop)
      : WinDesktopWindowPresenter{prop} {}
  ChatWindowState::ptr show_state;
  AE_OBJECT_REFLECT(AE_MMBR(show_state))
  template <typename D>
  void Load(ae::Version<0>, D& d) {
    d(base_, show_state);
  }
  template <typename D>
  void Save(ae::Version<0>, D& d) const {
    d(base_, show_state);
  }

  std::optional<bool> SavedMaximization() const override {
    return show_state->maximized;
  }
  void ObserveMaximized(bool maximized) override;
};

class ChatParticipantsPresenter
    : public ui::windows::WinListContainerPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::ChatParticipantsPresenter",
                           ChatParticipantsPresenter,
                           ui::windows::WinListContainerPresenter, 0)
 protected:
  ChatParticipantsPresenter() = default;

 public:
  explicit ChatParticipantsPresenter(ae::ObjProp prop)
      : WinListContainerPresenter{prop} {}
  template <typename D> void Load(ae::Version<0>, D& d) { d(base_); }
  template <typename D> void Save(ae::Version<0>, D& d) const { d(base_); }
  void OnLoad() override;
  void OnUnload() override;

 private:
  ui::windows::WinAccessibleName accessible_name_;
};

void EnsureChatWindowsPresenterRegistration();

}  // namespace apptraverse::chat
