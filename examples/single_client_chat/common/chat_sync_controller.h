#ifndef APPTRAVERSE_EXAMPLES_CHAT_SYNC_CONTROLLER_H_
#define APPTRAVERSE_EXAMPLES_CHAT_SYNC_CONTROLLER_H_

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "aether/clock.h"
#include "aether/types/uid.h"

#include "apptraverse/object_graph_copy.h"
#include "apptraverse/shared_graph_sync_session.h"
#include "apptraverse/sync_packet.h"

#include "model/chat.h"
#include "model/chat_peer_set.h"

namespace apptraverse::examples {

// Runtime manager for SharedGraphSyncSession instances of one Chat.
// Not persisted; peer identity stays in ChatPeerSet only.
class ChatSyncController {
 public:
  using SendFunction = std::function<void(
      ae::Uid const& peer, SerializedSyncPacket const& bytes)>;
  using ChangedFunction = std::function<void()>;
  using LogFunction = std::function<void(std::string const&)>;

  ChatSyncController(SyncReplica replica, Chat::ptr chat,
                     ChatPeerSet::ptr peer_set, SendFunction send,
                     bool auto_accept_incoming,
                     ChangedFunction changed = {}, LogFunction log = {});

  void Start();
  SharedGraphSyncSession& AddPeer(ae::Uid const& remote_uid);
  void Receive(ae::Uid const& remote_uid, SerializedSyncPacket const& bytes);
  void Tick(ae::TimePoint now);

  std::size_t runtime_session_count() const { return sessions_.size(); }
  SharedGraphSyncSession* FindSession(ae::Uid const& remote_uid);
  SharedGraphSyncSession const* FindSession(ae::Uid const& remote_uid) const;

 private:
  struct RuntimeSession {
    ae::Uid remote_uid{};
    std::unique_ptr<SharedGraphSyncSession> session;
    bool last_initial_sync_complete{false};
    ae::TimePoint last_retry{};
  };

  void Log(std::string const& line);
  void NotifyChanged();
  RuntimeSession& EnsureRuntimeSession(ae::Uid const& remote_uid,
                                       SyncSessionState::ptr state);
  void EmitInitialMarkers(RuntimeSession& runtime);

  SyncReplica replica_;
  Chat::ptr chat_;
  ChatPeerSet::ptr peer_set_;
  SendFunction send_;
  bool auto_accept_incoming_{false};
  ChangedFunction changed_;
  LogFunction log_;
  std::vector<RuntimeSession> sessions_;

  static constexpr auto kRetryInterval = std::chrono::seconds{1};
};

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_CHAT_SYNC_CONTROLLER_H_
