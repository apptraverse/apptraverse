#ifndef APPTRAVERSE_CHAT_COMPONENT_GRAPH_H_
#define APPTRAVERSE_CHAT_COMPONENT_GRAPH_H_

#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>

#include "aether/obj/obj.h"

#include "model/application_ids.h"
#include "model/chat.h"
#include "model/chat_events.h"
#include "model/chat_peer_set.h"
#include "model/client.h"

namespace apptraverse::chat {

enum class LocalJoinPolicy : std::uint8_t {
  kJoinLocal = 0,
  kDoNotJoinLocal = 1,
};

struct ChatComponentGraph {
  Chat::ptr chat_base;
  Chat::ptr chat;
  Client::ptr client_base;
  Client::ptr local_client;
  ChatPeerSet::ptr peer_set_base;
  ChatPeerSet::ptr peer_set;
};

// Create Chat / Client / ChatPeerSet and wire links. Does not CaptureBaseState
// or Commit JoinClientEvent — call FinalizeChatComponentGraph afterwards.
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

inline void CaptureChatComponentGraph(ChatComponentGraph& graph) {
  assert(graph.local_client.is_valid());
  assert(graph.peer_set.is_valid());
  assert(graph.chat.is_valid());
  assert(graph.chat.domain() != nullptr);

  graph.local_client->CaptureBaseState();
  graph.peer_set->CaptureBaseState();
  graph.chat->CaptureBaseState();
}

inline void CommitLocalJoinChatComponentGraph(ChatComponentGraph& graph) {
  assert(graph.local_client.is_valid());
  assert(graph.chat.is_valid());
  assert(graph.chat.domain() != nullptr);
  auto join = JoinClientEvent::ptr::Create(ae::CreateWith{*graph.chat.domain()});
  join->client = graph.local_client;
  graph.chat->Commit(join);
}

inline void FinalizeChatComponentGraph(ChatComponentGraph& graph,
                                       LocalJoinPolicy policy) {
  CaptureChatComponentGraph(graph);
  if (policy == LocalJoinPolicy::kJoinLocal) {
    CommitLocalJoinChatComponentGraph(graph);
  }
}

// Legacy helper: capture + join local participant.
inline void CaptureAndJoinChatComponentGraph(ChatComponentGraph& graph) {
  FinalizeChatComponentGraph(graph, LocalJoinPolicy::kJoinLocal);
}

// Headless Chat / Client / ChatPeerSet graph with fixed Chat IDs.
// Default preserves historical join-local behavior for Linux/Android/Apple.
inline ChatComponentGraph BuildChatComponentGraph(
    ae::Domain& domain, std::string_view local_name,
    LocalJoinPolicy policy = LocalJoinPolicy::kJoinLocal) {
  auto graph = MakeChatComponentGraph(domain, local_name);
  FinalizeChatComponentGraph(graph, policy);
  return graph;
}

}  // namespace apptraverse::chat

#endif  // APPTRAVERSE_CHAT_COMPONENT_GRAPH_H_
