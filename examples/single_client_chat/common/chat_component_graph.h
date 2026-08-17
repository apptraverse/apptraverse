#ifndef APPTRAVERSE_CHAT_COMPONENT_GRAPH_H_
#define APPTRAVERSE_CHAT_COMPONENT_GRAPH_H_

#include <cassert>
#include <string>
#include <string_view>

#include "aether/obj/obj.h"

#include "model/application_ids.h"
#include "model/chat.h"
#include "model/chat_events.h"
#include "model/chat_peer_set.h"
#include "model/client.h"

namespace apptraverse::chat {

struct ChatComponentGraph {
  Chat::ptr chat_base;
  Chat::ptr chat;
  Client::ptr client_base;
  Client::ptr local_client;
  ChatPeerSet::ptr peer_set_base;
  ChatPeerSet::ptr peer_set;
};

// Create Chat / Client / ChatPeerSet and wire links. Does not CaptureBaseState
// or Commit JoinClientEvent — call CaptureAndJoinChatComponentGraph after any
// optional demo wiring (e.g. chat->presenter).
inline ChatComponentGraph MakeChatComponentGraph(ae::Domain& domain,
                                                 std::string_view local_name) {
  ChatComponentGraph graph{};

  graph.chat_base = Chat::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ApplicationObjId::ChatBase)));
  graph.chat = Chat::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ApplicationObjId::Chat)));
  graph.client_base = Client::ptr::Create(ae::CreateWith{domain});
  graph.local_client = Client::ptr::Create(ae::CreateWith{domain});
  graph.peer_set_base = ChatPeerSet::ptr::Create(ae::CreateWith{domain});
  graph.peer_set = ChatPeerSet::ptr::Create(ae::CreateWith{domain});

  graph.local_client->name = std::string{local_name};

  graph.local_client->base = graph.client_base;
  graph.chat->base = graph.chat_base;
  graph.peer_set->base = graph.peer_set_base;
  graph.chat->peer_set = graph.peer_set;

  return graph;
}

inline void CaptureAndJoinChatComponentGraph(ChatComponentGraph& graph) {
  assert(graph.local_client.is_valid());
  assert(graph.peer_set.is_valid());
  assert(graph.chat.is_valid());
  assert(graph.chat.domain() != nullptr);

  graph.local_client->CaptureBaseState();
  graph.peer_set->CaptureBaseState();
  graph.chat->CaptureBaseState();

  auto join = JoinClientEvent::ptr::Create(ae::CreateWith{*graph.chat.domain()});
  join->client = graph.local_client;
  graph.chat->Commit(join);
}

// Headless Chat / Client / ChatPeerSet graph with fixed Chat IDs.
// No App / Window / Presenter objects.
inline ChatComponentGraph BuildChatComponentGraph(
    ae::Domain& domain, std::string_view local_name) {
  auto graph = MakeChatComponentGraph(domain, local_name);
  CaptureAndJoinChatComponentGraph(graph);
  return graph;
}

}  // namespace apptraverse::chat

#endif  // APPTRAVERSE_CHAT_COMPONENT_GRAPH_H_