#ifndef APPTRAVERSE_EXAMPLES_CHAT_COMPONENT_H_
#define APPTRAVERSE_EXAMPLES_CHAT_COMPONENT_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "aether/clock.h"
#include "aether/types/uid.h"

#include "apptraverse/object_graph_copy.h"
#include "apptraverse/shared_graph_sync_session.h"

#include "chat_presentation.h"
#include "chat_sync_controller.h"
#include "model/chat.h"
#include "model/chat_peer_set.h"
#include "model/chat_presenter.h"
#include "model/client.h"

namespace apptraverse::examples {

// Headless chat runtime: owns ChatSyncController and exposes a
// platform-neutral presentation snapshot.
//
// Lifecycle:
// - Start / Stop are idempotent.
// - Stop clears presentation subscribers and suppresses further callbacks
//   (generation_ bump).
// - No window / HWND / Activity; the host injects SyncReplica, model pointers,
//   and send functions.
// - Presentation-changed callbacks run on the caller / scheduler thread after
//   ChatComponent releases its locks.
class ChatComponent {
 public:
  using PresentationChangedFunction = std::function<void()>;
  using SubscriptionId = std::uint64_t;
  using SendFunction = ChatSyncController::SendFunction;
  using RawSendFunction = ChatSyncController::RawSendFunction;
  using LogFunction = ChatSyncController::LogFunction;

  ChatComponent(SyncReplica replica, Client::ptr local_client, Chat::ptr chat,
                ChatPresenter::ptr presenter, ChatPeerSet::ptr peer_set,
                SendFunction send, RawSendFunction raw_send,
                ChatSyncTiming timing, bool auto_accept_incoming,
                LogFunction log = {});

  ChatComponent(ChatComponent const&) = delete;
  ChatComponent& operator=(ChatComponent const&) = delete;

  ~ChatComponent();

  void Start();
  void Stop();
  bool is_running() const;

  Client::ptr const& local_client() const { return local_client_; }
  Chat::ptr const& chat() const { return chat_; }
  ChatPeerSet::ptr const& peer_set() const { return peer_set_; }
  ChatPresenter::ptr const& presenter() const { return presenter_; }

  // Returns a session reference only while running; otherwise nullopt.
  std::optional<std::reference_wrapper<SharedGraphSyncSession>> AddPeer(
      ae::Uid const& remote_uid);

  SharedGraphSyncSession* FindSession(ae::Uid const& remote_uid);
  SharedGraphSyncSession const* FindSession(ae::Uid const& remote_uid) const;
  std::size_t runtime_session_count() const;
  bool IsPeerOnline(ae::Uid const& remote_uid) const;

  void Receive(ae::Uid const& remote_uid,
               std::vector<std::uint8_t> const& bytes);
  void Tick(ae::TimePoint now);

  // Trims leading/trailing whitespace. Returns false if empty after trim,
  // not running, or presenter is unavailable.
  bool SubmitText(std::string text);

  ChatPresentationSnapshot CapturePresentation() const;

  SubscriptionId SubscribePresentationChanged(
      PresentationChangedFunction callback);
  void Unsubscribe(SubscriptionId id);

 private:
  struct Subscriber {
    SubscriptionId id{0};
    PresentationChangedFunction callback;
  };

  void NotifyPresentationChanged();
  ChatParticipantView MakeParticipantView(Client::ptr client) const;

  Client::ptr local_client_;
  Chat::ptr chat_;
  ChatPresenter::ptr presenter_;
  ChatPeerSet::ptr peer_set_;
  ChatSyncController sync_;

  mutable std::mutex mutex_;
  bool running_{false};
  std::atomic<std::uint64_t> generation_{0};
  SubscriptionId next_subscription_id_{1};
  std::vector<Subscriber> subscribers_;
};

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_CHAT_COMPONENT_H_