#ifndef APPTRAVERSE_CHAT_SHARED_H_
#define APPTRAVERSE_CHAT_SHARED_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "apptraverse/shared_event_id.h"
#include "apptraverse/shared_instance.h"
#include "apptraverse/shared_runtime.h"
#include "apptraverse/shared_transport.h"

#include "chat_model.h"
#include "chat_presence.h"
#include "chat_presence_overlay.h"

namespace apptraverse {

struct ChatSharedBinding {
  SharedInstance<ChatRoom> instance;
  SharedRuntime runtime;
  ChatPresenceOverlay presence;
};

struct LocalChatCommitResult {
  SharedEventId shared_event_id{};
  std::uint32_t local_event_obj_id{0};
  bool committed{false};
};

enum class SharedApplyResult : std::uint8_t {
  Applied = 1,
  DuplicateAlreadyApplied = 2,
  Deferred = 3,
  Rejected = 4,
};

inline bool SharedApplyResultAllowsAck(SharedApplyResult result) noexcept {
  return result == SharedApplyResult::Applied ||
         result == SharedApplyResult::DuplicateAlreadyApplied;
}

using OnNewChatClientFn = std::function<void(ChatClient& client)>;

void InitializeChatSharedBinding(ChatSharedBinding& binding, Application& app,
                                 std::string local_aether_uid);

void CommitLocalJoin(ChatSharedBinding& binding, ChatClient& client);
LocalChatCommitResult CommitLocalMessage(ChatSharedBinding& binding,
                                         ChatClient& author, std::string text,
                                         std::int64_t sent_at_unix_ms = 0);

SharedApplyResult ApplyIncomingSharedEvent(
    ChatSharedBinding& binding, std::string const& source_peer_uid,
    SharedEventFrame const& frame,
    std::function<void(std::string const& client_uid)> on_join_client = {},
    OnNewChatClientFn on_new_client = {});

// Creates/ensures Peer, seeds pending from Node journal. Does NOT open Aether
// streams and does NOT set channel_ready / online.
void EnsureSharedPeer(ChatSharedBinding& binding, std::string const& remote_uid);

// Ensures a ChatRoom.clients entry for a known remote UID so contacts can show
// locally-observed Presence without waiting on journal Join delivery. Does not
// commit journal events and does not open P2P.
ChatClient::ptr EnsurePresenceContact(ChatRoom& room,
                                      std::string const& remote_uid);

using OpenPeerRequestFn = std::function<void(std::string const& remote_uid)>;

void ConnectToHostCommand(ChatSharedBinding& binding, std::string host_uid,
                          OpenPeerRequestFn request_open_peer);

void SetSharedPeerChannelReady(ChatSharedBinding& binding,
                               std::string const& remote_uid, bool ready);

// Pure Presence overlay update. Must not touch SharedRuntime peers, pending,
// in-flight, journal delivery, or transport.
bool SetRemotePresenceObservation(ChatSharedBinding& binding,
                                  std::string const& remote_uid,
                                  PresenceState state);

bool SetLocalPresenceObservation(ChatSharedBinding& binding,
                                 PresenceState state);

// Deprecated alias: forwards to SetRemotePresenceObservation (no peer seed).
void SetSharedPeerPresence(ChatSharedBinding& binding,
                           std::string const& remote_uid, PresenceState state);

void RequeueInFlightAfterWriteFailed(ChatSharedBinding& binding,
                                     std::string const& remote_uid);

// Returns number of ChatClient presentation values that changed. Does not
// unconditionally notify the ChatRoom.
std::size_t ApplyPresenceOverlay(ChatSharedBinding& binding);

// Total pending + in-flight shared delivery events across peers (runtime only).
std::size_t CountSharedPendingAndInFlight(ChatSharedBinding const& binding);

void HandleSharedAck(ChatSharedBinding& binding,
                     std::string const& from_peer_uid,
                     SharedAckFrame const& frame);

void TickSharedDelivery(ChatSharedBinding& binding,
                        std::chrono::steady_clock::time_point now,
                        ISharedTransport* transport);

void SendSharedAck(ChatSharedBinding& binding, ISharedTransport* transport,
                   std::string const& peer_uid, SharedEventId const& event_id);

std::vector<std::uint8_t> SerializeSharedEventPayload(Event const& event);

bool DeserializeSharedEventPayload(ChatRoom& room, Event::ptr& out_event,
                                   std::vector<std::uint8_t> const& payload,
                                   OnNewChatClientFn on_new_client = {},
                                   std::string fallback_author_uid = {});

void StripRuntimeFieldsFromEventGraph(Event& event);

Event::ptr RemapIncomingEvent(ChatRoom& room, ae::Domain& model_domain,
                              std::vector<std::uint8_t> const& payload,
                              OnNewChatClientFn on_new_client = {},
                              std::string fallback_author_uid = {});

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_SHARED_H_
