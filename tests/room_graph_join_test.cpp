#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "aether/domain_storage/ram_domain_storage.h"

#include "apptraverse/sync_packet.h"
#include "chat_component.h"
#include "chat_component_graph.h"
#include "model/chat_component_registration.h"
#include "model/chat_events.h"

namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

std::size_t CountJoins(apptraverse::Chat::ptr chat) {
  chat.Load();
  std::size_t n = 0;
  for (auto const& record : chat->journal) {
    if (record.event.is_valid() &&
        record.event->GetClassId() == apptraverse::JoinClientEvent::kClassId) {
      ++n;
    }
  }
  return n;
}

}  // namespace

int main() {
  apptraverse::EnsureChatComponentRegistration();
  using apptraverse::chat::BuildChatComponentGraph;
  using apptraverse::chat::ChatComponent;
  using apptraverse::chat::LocalJoinPolicy;

  {
    ae::RamDomainStorage storage;
    ae::Domain domain{ae::Now(), storage};
    auto graph =
        BuildChatComponentGraph(domain, "HostUser", LocalJoinPolicy::kJoinLocal);
    CHECK(CountJoins(graph.chat) == 1);
    auto noop_send = [](ae::Uid const&, ae::ObjId,
                        apptraverse::SerializedSyncPacket const&) {};
    auto noop_raw = [](ae::Uid const&, std::vector<std::uint8_t> const&) {};
    auto noop_connect = [](ae::Uid const&) {};
    ChatComponent component(
        apptraverse::SyncReplica{domain, storage, graph.chat.id()},
        graph.local_client, graph.chat, noop_send, noop_raw, noop_connect, {});
    component.Start();
    CHECK(component.HasLocalJoin());
    CHECK(component.SubmitText("hi").has_value());
  }

  {
    ae::RamDomainStorage storage;
    ae::Domain domain{ae::Now(), storage};
    auto graph = BuildChatComponentGraph(domain, "ClientOne",
                                         LocalJoinPolicy::kDoNotJoinLocal);
    CHECK(CountJoins(graph.chat) == 0);
    auto noop_send = [](ae::Uid const&, ae::ObjId,
                        apptraverse::SerializedSyncPacket const&) {};
    auto noop_raw = [](ae::Uid const&, std::vector<std::uint8_t> const&) {};
    auto noop_connect = [](ae::Uid const&) {};
    ChatComponent component(
        apptraverse::SyncReplica{domain, storage, graph.chat.id()},
        graph.local_client, graph.chat, noop_send, noop_raw, noop_connect, {});
    component.Start();
    CHECK(!component.HasLocalJoin());
    CHECK(!component.SubmitText("nope").has_value());
  }

  std::cout << "room_graph_join_test OK\n";
  return 0;
}
