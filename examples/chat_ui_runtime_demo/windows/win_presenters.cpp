#include "win_presenters.h"

#include "apptraverse/object_macros.h"

namespace chat::win32 {
namespace {

APPTRAVERSE_REGISTER(WinChatWindowPresenter);
APPTRAVERSE_REGISTER(WinChatPresentationApplication);

}  // namespace

void EnsureChatPresenterRegistration() {
  apptraverse::EnsureObjectRegistration();
}

WinChatPresentationApplication::ptr BuildPresentationGraph(
    ae::Domain& domain, ChatApplication& application) {
  auto root = WinChatPresentationApplication::ptr::Create(
      ae::CreateWith{domain}.with_id(
          ToObjId(ChatObjId::WinPresentationApplication)));
  auto chat = WinChatWindowPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(
          ToObjId(ChatObjId::WinChatWindowPresenter)));

  chat->room = application.room;
  chat->network = application.network;
  chat->aether = application.aether;
  chat->application = ChatApplication::ptr::MakeFromThis(&application);
  root->chat_window = chat;
  return root;
}

}  // namespace chat::win32
