#ifndef APPTRAVERSE_CHAT_PEER_EVENTS_H_
#define APPTRAVERSE_CHAT_PEER_EVENTS_H_

#include "aether/types/uid.h"

#include "apptraverse/event_for.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/sync_session_state.h"

#include "model/chat_peer_set.h"

namespace apptraverse {

class AddChatPeerEvent : public EventFor<ChatPeerSet, AddChatPeerEvent> {
  APPTRAVERSE_OBJECT(AddChatPeerEvent, Event, 0)

 protected:
  AddChatPeerEvent() = default;

 public:
  explicit AddChatPeerEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(remote_uid), AE_MMBR(session_state))

  ae::Uid remote_uid{};
  LocalPtr<SyncSessionState> session_state;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_PEER_EVENTS_H_
