#ifndef APPTRAVERSE_EXAMPLES_GRAPH_BUILDER_H_
#define APPTRAVERSE_EXAMPLES_GRAPH_BUILDER_H_

#include <string>
#include <string_view>
#include <utility>

#include "aether/obj/obj.h"

#include "model/app.h"
#include "model/application_ids.h"
#include "model/chat.h"
#include "model/chat_events.h"
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
};

// Build the shared AppTraverse single-client chat graph using platform
// concrete Window / WindowPresenter / ChatPresenter types.
// Chat / Window fixed ObjIds are shared roots; local Client and JoinClientEvent
// use generated ObjIds unique to this installation.
template <typename WindowT, typename WindowPresenterT, typename ChatPresenterT>
SingleClientChatGraph BuildSingleClientChatGraph(
    ae::Domain& domain, std::string_view local_client_name) {
  SingleClientChatGraph graph{};

  graph.app = App::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ApplicationObjId::Application)));
  graph.window_base = WindowT::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ApplicationObjId::WindowBase)));
  graph.window = WindowT::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ApplicationObjId::Window)));
  graph.window_presenter = WindowPresenterT::ptr::Create(ae::CreateWith{domain}
      .with_id(ToObjId(ApplicationObjId::WindowPresenter)));
  graph.chat_base = Chat::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ApplicationObjId::ChatBase)));
  graph.chat = Chat::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ApplicationObjId::Chat)));
  graph.chat_presenter = ChatPresenterT::ptr::Create(ae::CreateWith{domain}
      .with_id(ToObjId(ApplicationObjId::ChatPresenter)));
  graph.client_base = Client::ptr::Create(ae::CreateWith{domain});
  graph.local_client = Client::ptr::Create(ae::CreateWith{domain});

  graph.local_client->name = std::string{local_client_name};

  graph.app->window = graph.window;
  graph.app->local_client = graph.local_client;
  graph.window->presenter = graph.window_presenter;
  graph.window->chat = graph.chat;
  graph.window_presenter->window = graph.window;
  graph.window_presenter->chat_presenter = graph.chat_presenter;

  graph.window->base = graph.window_base;
  graph.local_client->base = graph.client_base;
  graph.chat->base = graph.chat_base;
  graph.chat->presenter = graph.chat_presenter;

  graph.window->CaptureBaseState();
  graph.local_client->CaptureBaseState();
  graph.chat->CaptureBaseState();

  graph.chat_presenter->chat = graph.chat;
  graph.chat_presenter->local_client = graph.local_client;

  auto join = JoinClientEvent::ptr::Create(ae::CreateWith{domain});
  join->client = graph.local_client;
  graph.chat->Commit(join);

  return graph;
}

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_GRAPH_BUILDER_H_
