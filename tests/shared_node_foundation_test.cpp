#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <type_traits>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/link.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/shared_event_order.h"
#include "apptraverse/shared_network_graph.h"
#include "apptraverse/shared_node.h"

#include "shared_node_demo_model.h"

namespace apptraverse::test {
namespace {

using apptraverse::example::shared_node::Client;
using apptraverse::example::shared_node::EnsureSharedNodeDemoRegistration;
using apptraverse::example::shared_node::SetValueEvent;
using apptraverse::example::shared_node::SharedValueNode;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

void SetValue(SharedValueNode& node, std::int32_t value) {
  auto event = SetValueEvent::ptr::Create(ae::CreateWith{*node.domain});
  event->value = value;
  node.Commit(event);
}

MemoryLink::ptr MakeMemoryLink(ae::Domain& domain, ae::ObjId id,
                               std::string endpoint) {
  auto link = MemoryLink::ptr::Create(ae::CreateWith{domain}.with_id(id));
  InitializeRuntimeNode(*link);
  link->endpoint_uid = std::move(endpoint);
  link->heartbeat_interval_ms = 1000;
  link.Save();
  return link;
}

void TestLinkPersistentSaveLoad() {
  ae::RamDomainStorage storage;
  ae::ObjId::Type const link_id = 11;
  {
    ae::Domain domain{storage};
    auto link = MakeMemoryLink(domain, ae::ObjId{link_id}, "endpoint-a");
    link.Save();
  }
  {
    ae::Domain domain{storage};
    auto link =
        MemoryLink::ptr::Declare(ae::CreateWith{domain}.with_id(link_id));
    link.Load();
    CHECK(link.is_loaded());
    CHECK(link->endpoint_uid == "endpoint-a");
    CHECK(link->heartbeat_interval_ms == 1000);
  }
}

void TestMultipleRefsSameLinkAfterRestart() {
  ae::RamDomainStorage storage;
  ae::ObjId::Type const link_id = 21;
  ae::ObjId::Type const node_a_id = 22;
  ae::ObjId::Type const node_b_id = 23;
  ae::ObjId::Type const client_id = 24;
  {
    ae::Domain domain{storage};
    auto link = MakeMemoryLink(domain, ae::ObjId{link_id}, "shared-x");
    auto node_a = SharedValueNode::ptr::Create(
        ae::CreateWith{domain}.with_id(node_a_id));
    auto node_b = SharedValueNode::ptr::Create(
        ae::CreateWith{domain}.with_id(node_b_id));
    auto client =
        Client::ptr::Create(ae::CreateWith{domain}.with_id(client_id));
    InitializeRuntimeNode(*node_a);
    InitializeRuntimeNode(*node_b);
    InitializeRuntimeNode(*client);
    node_a->AddShare(link, ShareAccess::ReadWrite);
    node_b->AddShare(link, ShareAccess::ReadOnly);
    client->name = "fixture-client";
    client->link = link;
    node_a.Save();
    node_b.Save();
    client.Save();
    link.Save();
  }
  {
    ae::Domain domain{storage};
    auto node_a = SharedValueNode::ptr::Declare(
        ae::CreateWith{domain}.with_id(node_a_id));
    auto node_b = SharedValueNode::ptr::Declare(
        ae::CreateWith{domain}.with_id(node_b_id));
    auto client =
        Client::ptr::Declare(ae::CreateWith{domain}.with_id(client_id));
    node_a.Load();
    node_b.Load();
    client.Load();
    CHECK(node_a->shares.size() == 1);
    CHECK(node_b->shares.size() == 1);
    CHECK(client->link.is_valid());
    node_a->shares[0].link.Load();
    node_b->shares[0].link.Load();
    client->link.Load();
    CHECK(node_a->shares[0].link.id().id() == link_id);
    CHECK(node_b->shares[0].link.id().id() == link_id);
    CHECK(client->link.id().id() == link_id);
    // Same Domain identity map entry — one C++ instance.
    CHECK(node_a->shares[0].link.operator->() ==
          node_b->shares[0].link.operator->());
    CHECK(client->link.operator->() == node_a->shares[0].link.operator->());
    MemoryLink::ptr memory = node_a->shares[0].link;
    memory.Load();
    CHECK(memory->endpoint_uid == "shared-x");
  }
}

void TestShareTopologyEventsAndPersistence() {
  ae::RamDomainStorage storage;
  ae::ObjId::Type const node_id = 31;
  ae::ObjId::Type const link_a_id = 32;
  ae::ObjId::Type const link_b_id = 33;
  {
    ae::Domain domain{storage};
    auto node =
        SharedValueNode::ptr::Create(ae::CreateWith{domain}.with_id(node_id));
    InitializeRuntimeNode(*node);
    auto link_a = MakeMemoryLink(domain, ae::ObjId{link_a_id}, "a");
    auto link_b = MakeMemoryLink(domain, ae::ObjId{link_b_id}, "b");
    node->AddShare(link_a, ShareAccess::ReadWrite);
    node->AddShare(link_b, ShareAccess::ReadWrite);
    CHECK(node->shares.size() == 2);
    CHECK(node->journal.size() == 2);
    // Duplicate AddShare is a documented no-op (one share per Link).
    node->AddShare(link_a, ShareAccess::ReadOnly);
    CHECK(node->shares.size() == 2);
    CHECK(node->journal.size() == 2);
    node->SetShareAccess(link_b, ShareAccess::ReadOnly);
    CHECK(node->shares[1].GetAccess() == ShareAccess::ReadOnly);
    node->RemoveShare(link_a);
    CHECK(node->shares.size() == 1);
    CHECK(node->shares[0].link.id().id() == link_b_id);
    node.Save();
  }
  {
    ae::Domain domain{storage};
    auto node =
        SharedValueNode::ptr::Declare(ae::CreateWith{domain}.with_id(node_id));
    node.Load();
    CHECK(node->shares.size() == 1);
    CHECK(node->shares[0].link.id().id() == link_b_id);
    CHECK(node->shares[0].GetAccess() == ShareAccess::ReadOnly);
  }
}

void TestLocalSyncStatePersistsAndSurvivesReplay() {
  ae::RamDomainStorage storage;
  ae::ObjId::Type const node_id = 41;
  ae::ObjId::Type const link_id = 42;
  {
    ae::Domain domain{storage};
    auto node =
        SharedValueNode::ptr::Create(ae::CreateWith{domain}.with_id(node_id));
    InitializeRuntimeNode(*node);
    auto link = MakeMemoryLink(domain, ae::ObjId{link_id}, "bob");
    node->AddShare(link, ShareAccess::ReadWrite);
    SetValue(*node, 1);
    SetValue(*node, 2);
    node->SetInitialSyncPhase(link, InitialSyncPhase::Complete);
    CHECK(node->GetInitialSyncPhase(link) == InitialSyncPhase::Complete);
    CHECK(node->link_sync_states.size() == 1);
    CHECK(node->journal.size() >= 2);
    auto const second_lp = node->journal.back().order.lamport;
    auto const first_lp =
        node->journal[node->journal.size() - 2].order.lamport;
    CHECK(second_lp > first_lp);

    // Mid-journal insert forces RebuildFromBaseAndReplay.
    auto mid = SetValueEvent::ptr::Create(ae::CreateWith{domain});
    mid->value = 3;
    node->InsertAtForTest(
        SharedEventOrder{.lamport = first_lp + (second_lp - first_lp) / 2},
        mid);
    // Order: 1, 3, 2 → materialized value 2.
    CHECK(node->value == 2);
    CHECK(node->GetInitialSyncPhase(link) == InitialSyncPhase::Complete);
    node.Save();
    for (auto& entry : node->link_sync_states) {
      entry.Save();
    }
  }
  {
    ae::Domain domain{storage};
    auto node =
        SharedValueNode::ptr::Declare(ae::CreateWith{domain}.with_id(node_id));
    node.Load();
    CHECK(node->value == 2);
    CHECK(node->link_sync_states.size() == 1);
    node->link_sync_states[0].Load();
    CHECK(node->link_sync_states[0]->GetInitialSyncPhase() ==
          InitialSyncPhase::Complete);
  }
}

void TestNetworkSerializationBoundaries() {
  ae::RamDomainStorage source_storage;
  ae::ObjId::Type const node_id = 51;
  ae::ObjId::Type const link_a_id = 52;
  ae::ObjId::Type const link_b_id = 53;
  void const* source_node_addr = nullptr;
  void const* source_link_addr = nullptr;
  {
    ae::Domain domain{source_storage};
    auto node =
        SharedValueNode::ptr::Create(ae::CreateWith{domain}.with_id(node_id));
    InitializeRuntimeNode(*node);
    auto link_a = MakeMemoryLink(domain, ae::ObjId{link_a_id}, "alice");
    auto link_b = MakeMemoryLink(domain, ae::ObjId{link_b_id}, "bob");
    node->AddShare(link_a, ShareAccess::ReadWrite);
    node->AddShare(link_b, ShareAccess::ReadWrite);
    SetValue(*node, 42);
    node->SetInitialSyncPhase(link_b, InitialSyncPhase::Complete);
    CHECK(node->GetInitialSyncPhase(link_b) == InitialSyncPhase::Complete);
    CHECK(node->link_sync_states.size() == 1);
    source_node_addr = static_cast<void const*>(node.operator->());
    source_link_addr = static_cast<void const*>(link_b.operator->());
    node.Save();
    link_a.Save();
    link_b.Save();
    for (auto& entry : node->link_sync_states) {
      entry.Save();
    }

    // Live source still has local sync after network copy.
    ae::RamDomainStorage target_storage;
    ae::Domain target_domain{target_storage};
    CopySharedNetworkGraph(node, source_storage, target_domain, target_storage);
    CHECK(node->link_sync_states.size() == 1);
    CHECK(node->GetInitialSyncPhase(link_b) == InitialSyncPhase::Complete);

    auto imported = SharedValueNode::ptr::Declare(
        ae::CreateWith{target_domain}.with_id(node_id));
    imported.Load();
    CHECK(imported.is_loaded());
    CHECK(static_cast<void const*>(imported.operator->()) != source_node_addr);
    CHECK(imported->value == 42);
    CHECK(imported->shares.size() == 2);
    imported->shares[0].link.Load();
    imported->shares[1].link.Load();
    CHECK(imported->shares[0].link.id().id() == link_a_id ||
          imported->shares[0].link.id().id() == link_b_id);
    CHECK(imported->shares[1].link.id().id() == link_a_id ||
          imported->shares[1].link.id().id() == link_b_id);
    CHECK(static_cast<void const*>(imported->shares[0].link.operator->()) !=
          source_link_addr);
    // Local sync metadata must not arrive with the network graph.
    CHECK(imported->link_sync_states.empty());
    CHECK(imported->GetInitialSyncPhase(imported->shares[0].link) ==
          InitialSyncPhase::NotStarted);

    // Receiver creates its own local sync state independently.
    imported->SetInitialSyncPhase(imported->shares[0].link,
                                  InitialSyncPhase::Pending);
    CHECK(imported->GetInitialSyncPhase(imported->shares[0].link) ==
          InitialSyncPhase::Pending);
    CHECK(node->GetInitialSyncPhase(link_b) == InitialSyncPhase::Complete);
  }
}

void TestNoSerializedIsLocalAndNoRttiSurface() {
  static_assert(!std::is_same_v<SharedPtr<Link>, LocalPtr<Link>>);
  static_assert(LocalPtr<LinkSyncState>::kScope == LinkScope::kLocal);
  static_assert(!std::is_same_v<ShareAccess, bool>);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::example::shared_node::EnsureSharedNodeDemoRegistration();

  apptraverse::test::TestLinkPersistentSaveLoad();
  apptraverse::test::TestMultipleRefsSameLinkAfterRestart();
  apptraverse::test::TestShareTopologyEventsAndPersistence();
  apptraverse::test::TestLocalSyncStatePersistsAndSurvivesReplay();
  apptraverse::test::TestNetworkSerializationBoundaries();
  apptraverse::test::TestNoSerializedIsLocalAndNoRttiSurface();

  std::cout << "shared_node_foundation_test OK\n";
  return 0;
}
