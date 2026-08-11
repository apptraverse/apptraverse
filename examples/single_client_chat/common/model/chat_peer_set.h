#ifndef APPTRAVERSE_CHAT_PEER_SET_H_
#define APPTRAVERSE_CHAT_PEER_SET_H_

#include <cassert>
#include <vector>

#include "aether/types/uid.h"

#include "apptraverse/node_for.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/sync_session_state.h"

namespace apptraverse {

class AddChatPeerEvent;
class ChatPeerSet;

// Local transport binding for one remote Aether peer. Never synchronized.
struct ChatPeer {
  ae::Uid remote_uid{};
  LocalPtr<SyncSessionState> session_state;

  AE_REFLECT_MEMBERS(remote_uid, session_state)
};

class ChatPeerSet : public NodeFor<ChatPeerSet> {
  APPTRAVERSE_OBJECT(ChatPeerSet, Node, 0)

 protected:
  ChatPeerSet() = default;

 public:
  explicit ChatPeerSet(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(peers))

  std::vector<ChatPeer> peers;

  void Apply(AddChatPeerEvent const& event);

  ChatPeer const* Find(ae::Uid const& remote_uid) const {
    for (auto const& peer : peers) {
      if (peer.remote_uid == remote_uid) {
        return &peer;
      }
    }
    return nullptr;
  }

  ChatPeer* Find(ae::Uid const& remote_uid) {
    for (auto& peer : peers) {
      if (peer.remote_uid == remote_uid) {
        return &peer;
      }
    }
    return nullptr;
  }
};

// Create SyncSessionState + Commit AddChatPeerEvent, or return existing peer.
ChatPeer const& AddChatPeer(ChatPeerSet::ptr peer_set, ae::ObjId chat_id,
                            ae::Uid remote_uid);

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_PEER_SET_H_
