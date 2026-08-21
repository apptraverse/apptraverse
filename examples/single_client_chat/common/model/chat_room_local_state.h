#ifndef APPTRAVERSE_CHAT_ROOM_LOCAL_STATE_H_
#define APPTRAVERSE_CHAT_ROOM_LOCAL_STATE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "aether-miscpp/reflect/reflect.h"

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "model/application_ids.h"

namespace apptraverse {

enum class ChatRoomRole : std::uint8_t {
  kHost = 0,
  kClient = 1,
};

// Value type persisted inside ChatRoomLocalState. Not an ae::Obj.
struct ChatRoomParticipantRecord {
  std::string uid;
  std::uint32_t client_obj_id{0};
  std::string display_name;

  AE_REFLECT_MEMBERS(uid, client_obj_id, display_name)
};

// Local-only room metadata. Fixed ObjId. Not part of shared Chat sync graph.
class ChatRoomLocalState : public NodeFor<ChatRoomLocalState> {
  APPTRAVERSE_OBJECT(ChatRoomLocalState, Node, 0)

 protected:
  ChatRoomLocalState() = default;

 public:
  explicit ChatRoomLocalState(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(role), AE_MMBR(local_client_obj_id),
                    AE_MMBR(local_display_name), AE_MMBR(host_uid),
                    AE_MMBR(active_membership_revision),
                    AE_MMBR(active_participants))

  ChatRoomRole role{ChatRoomRole::kHost};
  std::uint32_t local_client_obj_id{0};
  std::string local_display_name;
  std::string host_uid;
  std::uint64_t active_membership_revision{0};
  std::vector<ChatRoomParticipantRecord> active_participants;
};

inline ChatRoomLocalState::ptr DeclareChatRoomLocalState(ae::Domain& domain) {
  return ChatRoomLocalState::ptr::Declare(
      ae::CreateWith{domain}.with_id(
          ToObjId(ApplicationObjId::ChatRoomLocalState)));
}

inline ChatRoomLocalState::ptr LoadChatRoomLocalState(ae::Domain& domain) {
  auto ptr = DeclareChatRoomLocalState(domain);
  ptr.Load();
  return ptr;
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_ROOM_LOCAL_STATE_H_
