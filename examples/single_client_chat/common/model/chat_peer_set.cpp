#include "model/chat_peer_set.h"

#include <utility>

#include "model/chat_peer_events.h"

namespace apptraverse {

void ChatPeerSet::Apply(AddChatPeerEvent const& event) {
  assert(!event.remote_uid.empty());
  assert(event.session_state.is_valid());
  assert(Find(event.remote_uid) == nullptr);
  ChatPeer peer{};
  peer.remote_uid = event.remote_uid;
  peer.session_state = event.session_state;
  peers.push_back(std::move(peer));
}

ChatPeer const& AddChatPeer(ChatPeerSet::ptr peer_set, ae::ObjId chat_id,
                            ae::Uid remote_uid) {
  assert(peer_set.is_valid());
  assert(peer_set.is_loaded());
  assert(!remote_uid.empty());
  assert(chat_id.is_valid());
  assert(peer_set.domain() != nullptr);

  if (auto* existing = peer_set->Find(remote_uid)) {
    return *existing;
  }

  auto session_state =
      CreateSyncSessionState(*peer_set.domain(), chat_id);
  session_state.Save();

  auto event =
      AddChatPeerEvent::ptr::Create(ae::CreateWith{*peer_set.domain()});
  event->remote_uid = remote_uid;
  event->session_state = session_state;
  peer_set->Commit(std::move(event));
  peer_set.Save();

  auto* added = peer_set->Find(remote_uid);
  assert(added != nullptr);
  return *added;
}

}  // namespace apptraverse
