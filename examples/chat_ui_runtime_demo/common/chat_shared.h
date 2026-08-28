#ifndef APPTRAVERSE_CHAT_SHARED_H_
#define APPTRAVERSE_CHAT_SHARED_H_

#include <functional>
#include <string>
#include <vector>

#include "apptraverse/shared_instance.h"
#include "apptraverse/shared_runtime.h"
#include "apptraverse/shared_transport.h"

#include "chat_model.h"

namespace apptraverse {

struct ChatSharedBinding {
  SharedInstance<ChatRoom> instance;
  SharedRuntime runtime;
};

void InitializeChatSharedBinding(ChatSharedBinding& binding, Application& app,
                                 std::string local_aether_uid);

void CommitLocalJoin(ChatSharedBinding& binding, ChatClient& client);
void CommitLocalMessage(ChatSharedBinding& binding, ChatClient& author,
                        std::string text);

bool ApplyIncomingSharedEvent(ChatSharedBinding& binding,
                              std::string const& source_peer_uid,
                              SharedEventFrame const& frame,
                              std::function<void(std::string const& client_uid)>
                                  on_join_client = {});

// Creates/ensures Peer, seeds pending from journal. Does NOT open Aether
// streams and does NOT set channel_ready / online.
void EnsureSharedPeer(ChatSharedBinding& binding, std::string const& remote_uid);

// Model-thread Connect: validate, set room id, EnsurePeer+seed, request open.
using OpenPeerRequestFn = std::function<void(std::string const& remote_uid)>;

void ConnectToHostCommand(ChatSharedBinding& binding, std::string host_uid,
                          OpenPeerRequestFn request_open_peer);

void SetSharedPeerChannelReady(ChatSharedBinding& binding,
                               std::string const& remote_uid, bool ready);

void HandleSharedAck(ChatSharedBinding& binding,
                     std::string const& from_peer_uid,
                     SharedAckFrame const& frame);

void TickSharedDelivery(ChatSharedBinding& binding,
                        std::chrono::steady_clock::time_point now,
                        ISharedTransport* transport);

void SendSharedAck(ChatSharedBinding& binding, ISharedTransport* transport,
                   std::string const& peer_uid, SharedEventId const& event_id);

std::vector<std::uint8_t> SerializeSharedEventPayload(Event const& event);

// Deserializes payload into a remapped Event for `room` (cross-process safe).
bool DeserializeSharedEventPayload(ChatRoom& room, Event::ptr& out_event,
                                   std::vector<std::uint8_t> const& payload);

void StripRuntimeFieldsFromEventGraph(Event& event);

Event::ptr RemapIncomingEvent(ChatRoom& room, ae::Domain& model_domain,
                              std::vector<std::uint8_t> const& payload);

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_SHARED_H_
