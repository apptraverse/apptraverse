#ifndef APPTRAVERSE_CHAT_COMPONENT_H_
#define APPTRAVERSE_CHAT_COMPONENT_H_

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "aether/clock.h"
#include "aether/types/uid.h"

#include "aether/obj/idomain_storage.h"

#include "apptraverse/object_graph_copy.h"

#include "chat_presentation.h"
#include "chat_sync_controller.h"
#include "model/chat.h"
#include "model/chat_peer_set.h"
#include "model/client.h"

namespace apptraverse::chat {

enum class AddPeerResult : std::uint8_t {
  kAdded = 0,
  kAlreadyPresent = 1,
  kNotRunning = 2,
  kInvalidUid = 3,
};

// Headless chat runtime: owns ChatSyncController and exposes a
// platform-neutral presentation snapshot. Sole chat boundary for hosts.
class ChatComponent {
 public:
  using PresentationChangedFunction = std::function<void()>;
  using SubscriptionId = std::uint64_t;
  using SendFunction = ChatSyncController::SendFunction;
  using RawSendFunction = ChatSyncController::RawSendFunction;
  // Ensures a host transport session exists; not a wait-for-connected handshake.
  using ConnectFunction = std::function<void(ae::Uid const& remote_uid)>;
  using LogFunction = ChatSyncController::LogFunction;

  ChatComponent(SyncReplica replica,
                Client::ptr local_client,
                Chat::ptr chat,
                SendFunction send,
                RawSendFunction raw_send,
                ConnectFunction connect,
                ChatSyncTiming timing,
                LogFunction log = {});

  ChatComponent(ChatComponent const&) = delete;
  ChatComponent& operator=(ChatComponent const&) = delete;
  ~ChatComponent();

  void Start();
  void Stop();
  bool is_running() const;

  AddPeerResult AddPeer(ae::Uid const& remote_uid);
  std::optional<std::uint32_t> SubmitText(std::string text);
  // True only when journal Join matches local Client by ObjId (activation path).
  bool HasLocalJoin() const;
  // Probe Join identity: ObjId (normal) vs name fallback (migration/debug only).
  enum class LocalJoinMatchKind : std::uint8_t {
    kNone = 0,
    kObjId = 1,
    kNameFallback = 2,
  };
  struct LocalJoinProbe {
    LocalJoinMatchKind kind{LocalJoinMatchKind::kNone};
    std::uint32_t local_client_obj_id{0};
    std::uint32_t join_client_obj_id{0};
  };
  LocalJoinProbe ProbeLocalJoin() const;
  // Publish a journal event committed outside SubmitText (e.g. host room Join).
  void PublishCommittedJournalEvent(EventRecord const& record);

  void SetIncomingPeerAuthorize(
      ChatSyncController::IncomingPeerAuthorizeFunction fn);

  void Receive(ae::Uid const&, std::vector<std::uint8_t> const&);
  void Tick(ae::TimePoint);
  void NotifyTransportSessionReady(ae::Uid const& remote_uid,
                                   std::uint64_t transport_generation);

  ChatPresentationSnapshot CapturePresentation() const;

  SubscriptionId SubscribePresentationChanged(PresentationChangedFunction);
  void Unsubscribe(SubscriptionId);

 private:
  struct Subscriber {
    SubscriptionId id{0};
    PresentationChangedFunction callback;
  };

  void NotifyPresentationChanged();
  ChatParticipantView MakeParticipantView(Client::ptr client) const;

  ae::IDomainStorage* storage_{nullptr};
  Client::ptr local_client_;
  Chat::ptr chat_;
  ChatPeerSet::ptr peer_set_;
  ConnectFunction connect_;
  ChatSyncController sync_;

  bool running_{false};
  SubscriptionId next_subscription_id_{1};
  std::vector<Subscriber> subscribers_;
};

}  // namespace apptraverse::chat

#endif  // APPTRAVERSE_CHAT_COMPONENT_H_