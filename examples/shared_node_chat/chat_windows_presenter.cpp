#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif
#include "chat_windows_presenter.h"

#include "apptraverse/model_object_proxy.h"
#include "chat_ui_model.h"

namespace apptraverse::chat {

void ChatWindowState::SetMaximized(bool next) {
  if (maximized == next) return;
  auto event = SetChatWindowMaximized::ptr::Create(ae::CreateWith{*domain});
  event->value = next;
  CommitLocal(event);
}

void ChatWindowState::Apply(SetChatWindowMaximized const& event) {
  maximized = event.value;
  NoteMaterializedChange();
}

void ChatDesktopWindowPresenter::ObserveMaximized(bool maximized) {
  model_proxy->Invoke(show_state.id(), &ChatWindowState::SetMaximized,
                      maximized);
}

void ChatParticipantsPresenter::OnLoad() {
  WinListContainerPresenter::OnLoad();
  auto const& chat = static_cast<gui::ChatUi const&>(*window_presenter->window);
  accessible_name_.Set(hwnd(), chat.Text(gui::Label::People));
}

void ChatParticipantsPresenter::OnUnload() {
  accessible_name_.Clear(hwnd());
  WinListContainerPresenter::OnUnload();
}

APPTRAVERSE_REGISTER(ChatParticipantsPresenter);
APPTRAVERSE_REGISTER(ChatWindowState);
APPTRAVERSE_REGISTER(SetChatWindowMaximized);
APPTRAVERSE_REGISTER(ChatDesktopWindowPresenter);
void EnsureChatWindowsPresenterRegistration() {
  (void)&g_apptraverse_registrar_ChatParticipantsPresenter;
  (void)&g_apptraverse_registrar_ChatWindowState;
  (void)&g_apptraverse_registrar_SetChatWindowMaximized;
  (void)&g_apptraverse_registrar_ChatDesktopWindowPresenter;
}

}  // namespace apptraverse::chat
