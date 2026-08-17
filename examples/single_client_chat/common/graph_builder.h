#ifndef APPTRAVERSE_EXAMPLES_GRAPH_BUILDER_H_
#define APPTRAVERSE_EXAMPLES_GRAPH_BUILDER_H_

#include <string>
#include <string_view>
#include <utility>

#include "aether/obj/obj.h"

#include "chat_component_graph.h"
#include "model/app.h"
#include "model/application_ids.h"
#include "model/chat.h"
#include "model/chat_peer_set.h"
#include "model/chat_presenter.h"
#include "model/client.h"
#include "model/window.h"
#include "model/window_presenter.h"

namespace apptraverse::examples {

struct SingleClientChatGraph {
  App::ptr app;
  Window::ptr window_base;
  Window::ptr window;
  WindowPresenter::ptr window_presenter;
  Chat::ptr chat_base;
  Chat::ptr chat;
  ChatPresenter::ptr chat_presenter;
  Client::ptr client_base;
  Client::ptr local_client;
  ChatPeerSet::ptr peer_set_base;
  ChatPeerSet::ptr peer_set;
};

// Build the shared AppTraverse single-client chat graph using platform
// concrete Window / WindowPresenter / ChatPresenter types.
// Chat / Window fixed ObjIds are shared roots; local Client and JoinClientEvent
// use generated ObjIds unique to this installation.
template <typename WindowT, typename WindowPresenterT, typename ChatPresenterT>
SingleClientChatGraph BuildSingleClientChatGraph(
    ae::Domain& domain, std::string_view local_client_name) {
  SingleClientChatGraph graph{};

  auto core = chat::MakeChatComponentGraph(domain, local_client_name);
  graph.chat_base = core.chat_base;
  graph.chat = core.chat;
  graph.client_base = core.client_base;
  graph.local_client = core.local_client;
  graph.peer_set_base = core.peer_set_base;
  graph.peer_set = core.peer_set;

  graph.app = App::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ApplicationObjId::Application)));
  graph.window_base = WindowT::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ApplicationObjId::WindowBase)));
  graph.window = WindowT::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ApplicationObjId::Window)));
  graph.window_presenter = WindowPresenterT::ptr::Create(ae::CreateWith{domain}
      .with_id(ToObjId(ApplicationObjId::WindowPresenter)));
  graph.chat_presenter = ChatPresenterT::ptr::Create(ae::CreateWith{domain}
      .with_id(ToObjId(ApplicationObjId::ChatPresenter)));

  graph.app->window = graph.window;
  graph.app->local_client = graph.local_client;
  graph.window->presenter = graph.window_presenter;
  graph.window->chat = graph.chat;
  graph.window_presenter->window = graph.window;
  graph.window_presenter->chat_presenter = graph.chat_presenter;

  graph.window->base = graph.window_base;
  graph.chat->presenter = graph.chat_presenter;

  graph.window->CaptureBaseState();

  chat::ChatComponentGraph for_join{};
  for_join.chat = graph.chat;
  for_join.local_client = graph.local_client;
  for_join.peer_set = graph.peer_set;
  chat::CaptureAndJoinChatComponentGraph(for_join);

  graph.chat_presenter->chat = graph.chat;
  graph.chat_presenter->local_client = graph.local_client;

  return graph;
}

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_GRAPH_BUILDER_H_