#ifndef APPTRAVERSE_CHAT_SHARED_H_
#define APPTRAVERSE_CHAT_SHARED_H_

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
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

void EnsureSharedPeer(ChatSharedBinding& binding, std::string const& remote_uid,
                      ISharedTransport* transport);

void HandleSharedAck(ChatSharedBinding& binding,
                     std::string const& from_peer_uid,
                     SharedAckFrame const& frame);

void TickSharedDelivery(
    ChatSharedBinding& binding, std::chrono::steady_clock::time_point now,
    std::function<ISharedTransport*(std::string const& peer_uid)> const&
        transport_for_peer);

std::vector<std::uint8_t> SerializeSharedEventPayload(Event const& event);
bool DeserializeSharedEventPayload(ae::Domain& domain, Event::ptr& out_event,
                                   std::vector<std::uint8_t> const& payload);

void StripRuntimeFieldsFromEventGraph(Event& event);

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_SHARED_H_
