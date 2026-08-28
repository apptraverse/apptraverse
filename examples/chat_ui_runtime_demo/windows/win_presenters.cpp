#include "win_presenters.h"

#include "apptraverse/object_macros.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(WinChatWindowPresenter);
APPTRAVERSE_REGISTER(WinChatPresentationApplication);

}  // namespace

void EnsureChatPresenterRegistration() { EnsureObjectRegistration(); }

WinChatPresentationApplication::ptr BuildPresentationGraph(
    ae::Domain& domain, Application& application) {
  using chat::ChatObjId;
  using chat::ToObjId;

  auto root = WinChatPresentationApplication::ptr::Create(
      ae::CreateWith{domain}.with_id(
          ToObjId(ChatObjId::WinPresentationApplication)));
  auto chat = WinChatWindowPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(
          ToObjId(ChatObjId::WinChatWindowPresenter)));

  chat->room = application.chat_room;
  chat->identity = application.local_aether;
  chat->application = Application::ptr::MakeFromThis(&application);
  root->chat_window = chat;
  return root;
}

}  // namespace apptraverse
