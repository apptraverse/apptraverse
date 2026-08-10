#ifndef APPTRAVERSE_EXAMPLES_GRAPH_BUILDER_H_
#define APPTRAVERSE_EXAMPLES_GRAPH_BUILDER_H_

#include <string>
#include <utility>

#include "aether/obj/obj.h"

#include "apptraverse/app.h"
#include "apptraverse/application_ids.h"
#include "apptraverse/chat.h"
#include "apptraverse/chat_events.h"
#include "apptraverse/chat_presenter.h"
#include "apptraverse/client.h"
#include "apptraverse/window.h"
#include "apptraverse/window_presenter.h"

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
  Client::ptr alice;
};

// Build the shared AppTraverse single-client chat graph using platform
// concrete Window / WindowPresenter / ChatPresenter types.
template <typename WindowT, typename WindowPresenterT, typename ChatPresenterT>
SingleClientChatGraph BuildSingleClientChatGraph(ae::Domain& domain) {
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
  graph.client_base = Client::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ApplicationObjId::ClientBase)));
  graph.alice = Client::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ApplicationObjId::Alice)));

  graph.alice->name = "Alice";

  graph.app->window = graph.window;
  graph.window->presenter = graph.window_presenter;
  graph.window->chat = graph.chat;
  graph.window_presenter->window = graph.window;
  graph.window_presenter->chat_presenter = graph.chat_presenter;

  graph.window->base = graph.window_base;
  graph.alice->base = graph.client_base;
  graph.chat->base = graph.chat_base;
  graph.chat->presenter = graph.chat_presenter;

  graph.window->CaptureBaseState();
  graph.alice->CaptureBaseState();
  graph.chat->CaptureBaseState();

  graph.chat_presenter->chat = graph.chat;

  auto join = JoinClientEvent::ptr::Create(ae::CreateWith{domain}.with_id(
      ToObjId(ApplicationObjId::JoinClientEvent)));
  join->client = graph.alice;
  graph.chat->Commit(join);

  return graph;
}

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_GRAPH_BUILDER_H_
