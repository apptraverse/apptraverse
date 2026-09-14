#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/registry.h"

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

using apptraverse::example::shared_node::ChildSharedNode;
using apptraverse::example::shared_node::Client;
using apptraverse::example::shared_node::EnsureSharedNodeDemoRegistration;
using apptraverse::example::shared_node::LocalOnlyPayload;
using apptraverse::example::shared_node::LocalPtrHolder;
using apptraverse::example::shared_node::RootSharedNode;
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

class CountingDomainStorage final : public ae::IDomainStorage {
 public:
  explicit CountingDomainStorage(ae::IDomainStorage& inner) : inner_{inner} {}

  std::size_t store_count() const { return store_count_; }

  std::unique_ptr<ae::IDomainStorageWriter> Store(
      ae::DomainQuery const& query) override {
    ++store_count_;
    return inner_.Store(query);
  }

  ae::ClassList Enumerate(ae::ObjId const& obj_id) override {
    return inner_.Enumerate(obj_id);
  }

  ae::DomainLoad Load(ae::DomainQuery const& query) override {
    return inner_.Load(query);
  }

  void Remove(ae::ObjId const& obj_id) override { inner_.Remove(obj_id); }
  void CleanUp() override { inner_.CleanUp(); }

 private:
  ae::IDomainStorage& inner_;
  std::size_t store_count_{0};
};

bool HasValidLocalSyncEntry(SharedNode const& node) {
  for (auto const& entry : node.link_sync_states) {
    if (entry.is_valid()) {
      return true;
    }
  }
  return false;
}

bool StorageHasClass(ae::IDomainStorage& storage, ae::ObjId obj_id,
                     std::uint32_t class_id) {
  auto const classes = storage.Enumerate(obj_id);
  for (auto const cid : classes) {
    if (cid == class_id) {
      return true;
    }
  }
  return false;
}

void SetValue(SharedValueNode& node, std::int32_t value) {
  auto event = SetValueEvent::ptr::Create(ae::CreateWith{*node.domain});
  event->value = value;
  node.Commit(event);
}

// Persistent Link fields are set before the Node becomes live.
MemoryLink::ptr MakeMemoryLink(ae::Domain& domain, ae::ObjId id,
                               std::string endpoint) {
  auto link = MemoryLink::ptr::Create(ae::CreateWith{domain}.with_id(id));
  link->endpoint_uid = std::move(endpoint);
  link->heartbeat_interval_ms = 1000;
  InitializeRuntimeNode(*link);
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

void TestLinkConfigInitializedBeforeLive() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto link = MemoryLink::ptr::Create(ae::CreateWith{domain}.with_id(12));
  // Configure while not yet a live runtime Node.
  CHECK(!link->base.is_valid());
  link->endpoint_uid = "pre-live";
  link->heartbeat_interval_ms = 2500;
  InitializeRuntimeNode(*link);
  CHECK(link->base.is_valid());
  CHECK(link->endpoint_uid == "pre-live");
  CHECK(link->heartbeat_interval_ms == 2500);
  link.Save();

  ae::Domain domain2{storage};
  auto loaded =
      MemoryLink::ptr::Declare(ae::CreateWith{domain2}.with_id(12));
  loaded.Load();
  CHECK(loaded->endpoint_uid == "pre-live");
  CHECK(loaded->heartbeat_interval_ms == 2500);
}

void TestMultipleRefsSameLinkAfterRestart() {
  ae::RamDomainStorage storage;
  ae::ObjId::Type const link_id = 21;
  ae::ObjId::Type const node_a_id = 22;
  ae::ObjId::Type const node_b_id = 23;
  ae::ObjId::Type const client_id = 24;
  ae::ObjId share_a_id;
  ae::ObjId share_b_id;
  {
    ae::Domain domain{storage};
    auto link = MakeMemoryLink(domain, ae::ObjId{link_id}, "shared-x");
    auto node_a = SharedValueNode::ptr::Create(
        ae::CreateWith{domain}.with_id(node_a_id));
    auto node_b = SharedValueNode::ptr::Create(
        ae::CreateWith{domain}.with_id(node_b_id));
    auto client =
        Client::ptr::Create(ae::CreateWith{domain}.with_id(client_id));
    // Client reflected fields before InitializeRuntimeNode.
    client->name = "fixture-client";
    client->link = link;
    InitializeRuntimeNode(*node_a);
    InitializeRuntimeNode(*node_b);
    InitializeRuntimeNode(*client);
    node_a->AddShare(link, ShareAccess::ReadWrite);
    node_b->AddShare(link, ShareAccess::ReadOnly);
    share_a_id = node_a->shares[0].share_id;
    share_b_id = node_b->shares[0].share_id;

    // One Link, two SharedNodes: independent relationships and local sync.
    CHECK(share_a_id != share_b_id);
    node_a->SetInitialSyncPhase(link, InitialSyncPhase::Complete);
    CHECK(node_a->GetInitialSyncPhase(link) == InitialSyncPhase::Complete);
    CHECK(node_b->GetInitialSyncPhase(link) == InitialSyncPhase::NotStarted);
    CHECK(node_a->link_sync_states[0].id() !=
          node_b->link_sync_states[0].id());

    node_a.Save();
    node_b.Save();
    client.Save();
    link.Save();
    for (auto& entry : node_a->link_sync_states) {
      entry.Save();
    }
    for (auto& entry : node_b->link_sync_states) {
      entry.Save();
    }
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
    CHECK(node_a->shares[0].link.operator->() ==
          node_b->shares[0].link.operator->());
    CHECK(client->link.operator->() == node_a->shares[0].link.operator->());
    MemoryLink::ptr memory = node_a->shares[0].link;
    memory.Load();
    CHECK(memory->endpoint_uid == "shared-x");

    CHECK(node_a->shares[0].share_id == share_a_id);
    CHECK(node_b->shares[0].share_id == share_b_id);
    CHECK(node_a->GetInitialSyncPhase(node_a->shares[0].link) ==
          InitialSyncPhase::Complete);
    CHECK(node_b->GetInitialSyncPhase(node_b->shares[0].link) ==
          InitialSyncPhase::NotStarted);
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
    CHECK(node->link_sync_states.size() == 2);
    CHECK(node->journal.size() == 2);
    node->AddShare(link_a, ShareAccess::ReadOnly);
    CHECK(node->shares.size() == 2);
    CHECK(node->journal.size() == 2);
    node->SetShareAccess(link_b, ShareAccess::ReadOnly);
    CHECK(node->shares[1].GetAccess() == ShareAccess::ReadOnly);
    node->RemoveShare(link_a);
    CHECK(node->shares.size() == 1);
    CHECK(node->link_sync_states.size() == 1);
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

void TestLocalSyncStateEventDrivenSaveLoadAndReplay() {
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
    CHECK(node->GetInitialSyncPhase(link) == InitialSyncPhase::NotStarted);
    SetValue(*node, 1);
    SetValue(*node, 2);
    node->SetInitialSyncPhase(link, InitialSyncPhase::Complete);
    CHECK(node->GetInitialSyncPhase(link) == InitialSyncPhase::Complete);
    CHECK(node->link_sync_states.size() == 1);
    CHECK(node->link_sync_states[0]->journal.size() >= 1);
    CHECK(node->journal.size() >= 2);
    auto const second_lp = node->journal.back().order.lamport;
    auto const first_lp =
        node->journal[node->journal.size() - 2].order.lamport;
    CHECK(second_lp > first_lp);

    // Mid-journal business insert forces RebuildFromBaseAndReplay.
    auto mid = SetValueEvent::ptr::Create(ae::CreateWith{domain});
    mid->value = 3;
    node->InsertAtForTest(
        SharedEventOrder{.lamport = first_lp + (second_lp - first_lp) / 2},
        mid);
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

void TestRemoveShareAddShareResetsLocalSync() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto node =
      SharedValueNode::ptr::Create(ae::CreateWith{domain}.with_id(61));
  InitializeRuntimeNode(*node);
  auto link = MakeMemoryLink(domain, ae::ObjId{62}, "peer");
  node->AddShare(link, ShareAccess::ReadWrite);
  node->SetInitialSyncPhase(link, InitialSyncPhase::Complete);
  CHECK(node->GetInitialSyncPhase(link) == InitialSyncPhase::Complete);
  auto const old_share_id = node->shares[0].share_id;
  auto const old_sync_id = node->link_sync_states[0].id();

  node->RemoveShare(link);
  CHECK(node->shares.empty());
  CHECK(node->link_sync_states.empty());

  node->AddShare(link, ShareAccess::ReadWrite);
  CHECK(node->shares.size() == 1);
  CHECK(node->link_sync_states.size() == 1);
  CHECK(node->GetInitialSyncPhase(link) == InitialSyncPhase::NotStarted);
  // Same Link, new relationship: new identity and its own sync state.
  CHECK(node->shares[0].share_id != old_share_id);
  CHECK(node->link_sync_states[0].id() != old_sync_id);
  CHECK(node->link_sync_states[0]->share_id == node->shares[0].share_id);

  // Replay of Remove+Add must also leave NotStarted (materialized + journal).
  node.Save();
  for (auto& entry : node->link_sync_states) {
    entry.Save();
  }
  ae::Domain domain2{storage};
  auto loaded =
      SharedValueNode::ptr::Declare(ae::CreateWith{domain2}.with_id(61));
  loaded.Load();
  CHECK(loaded->shares.size() == 1);
  loaded->shares[0].link.Load();
  CHECK(loaded->GetInitialSyncPhase(loaded->shares[0].link) ==
        InitialSyncPhase::NotStarted);
}

// Remove + re-add over the same Link creates a second sharing relationship.
// A later older business Event forces RebuildFromBaseAndReplay: replaying the
// historical Add/Remove must not consume the second relationship's local sync.
void TestShareRelationshipIdentitySurvivesForcedReplay() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto node =
      SharedValueNode::ptr::Create(ae::CreateWith{domain}.with_id(111));
  InitializeRuntimeNode(*node);
  auto link = MakeMemoryLink(domain, ae::ObjId{112}, "replay-peer");

  SetValue(*node, 1);
  node->AddShare(link, ShareAccess::ReadWrite);
  node->SetInitialSyncPhase(link, InitialSyncPhase::Complete);
  auto const first_share_id = node->shares[0].share_id;
  auto const first_sync_id = node->link_sync_states[0].id();

  node->RemoveShare(link);
  node->AddShare(link, ShareAccess::ReadWrite);
  auto const second_share_id = node->shares[0].share_id;
  CHECK(second_share_id != first_share_id);
  CHECK(node->GetInitialSyncPhase(link) == InitialSyncPhase::NotStarted);
  node->SetInitialSyncPhase(link, InitialSyncPhase::Complete);
  auto const second_sync_id = node->link_sync_states[0].id();
  CHECK(second_sync_id != first_sync_id);
  SetValue(*node, 2);

  // Older business Event ahead of the journal head → RebuildFromBaseAndReplay.
  auto early = SetValueEvent::ptr::Create(ae::CreateWith{domain});
  early->value = 3;
  node->InsertAtForTest(
      SharedEventOrder{.lamport = node->journal[0].order.lamport - 1}, early);

  CHECK(node->value == 2);
  CHECK(node->shares.size() == 1);
  CHECK(node->shares[0].link.id() == link.id());
  CHECK(node->shares[0].share_id == second_share_id);
  CHECK(node->link_sync_states.size() == 1);
  CHECK(node->link_sync_states[0].id() == second_sync_id);
  CHECK(node->link_sync_states[0]->share_id == second_share_id);
  CHECK(node->GetInitialSyncPhase(link) == InitialSyncPhase::Complete);

  // Second relationship also survives destroy + reload of the Domain.
  node.Save();
  link.Save();
  for (auto& entry : node->link_sync_states) {
    entry.Save();
  }
  ae::Domain reloaded_domain{storage};
  auto reloaded =
      SharedValueNode::ptr::Declare(ae::CreateWith{reloaded_domain}.with_id(111));
  reloaded.Load();
  CHECK(reloaded->shares.size() == 1);
  CHECK(reloaded->shares[0].share_id == second_share_id);
  CHECK(reloaded->link_sync_states.size() == 1);
  reloaded->link_sync_states[0].Load();
  CHECK(reloaded->link_sync_states[0].id() == second_sync_id);
  CHECK(reloaded->link_sync_states[0]->share_id == second_share_id);
  reloaded->shares[0].link.Load();
  CHECK(reloaded->GetInitialSyncPhase(reloaded->shares[0].link) ==
        InitialSyncPhase::Complete);

  // Relationship identity comes from the persisted AddShareEvent, so a forced
  // rebuild after the storage round trip resolves to the same relationship.
  auto reloaded_early =
      SetValueEvent::ptr::Create(ae::CreateWith{reloaded_domain});
  reloaded_early->value = 4;
  reloaded->InsertAtForTest(
      SharedEventOrder{.lamport = reloaded->journal[0].order.lamport - 1},
      reloaded_early);
  CHECK(reloaded->value == 2);
  CHECK(reloaded->shares.size() == 1);
  CHECK(reloaded->shares[0].share_id == second_share_id);
  CHECK(reloaded->link_sync_states.size() == 1);
  CHECK(reloaded->link_sync_states[0].id() == second_sync_id);
  CHECK(reloaded->GetInitialSyncPhase(reloaded->shares[0].link) ==
        InitialSyncPhase::Complete);
}

void TestNetworkSerializationBoundaries() {
  ae::RamDomainStorage raw_source;
  CountingDomainStorage source_storage{raw_source};
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
    auto const share_a_id = node->shares[0].share_id;
    auto const share_b_id = node->shares[1].share_id;
    SetValue(*node, 42);
    node->SetInitialSyncPhase(link_b, InitialSyncPhase::Complete);
    CHECK(node->GetInitialSyncPhase(link_b) == InitialSyncPhase::Complete);
    CHECK(node->link_sync_states.size() == 2);
    source_node_addr = static_cast<void const*>(node.operator->());
    source_link_addr = static_cast<void const*>(link_b.operator->());
    node.Save();
    link_a.Save();
    link_b.Save();
    for (auto& entry : node->link_sync_states) {
      entry.Save();
    }

    auto const stores_before_copy = source_storage.store_count();

    ae::RamDomainStorage target_storage;
    ae::Domain target_domain{target_storage};
    CopySharedNetworkGraph(node, target_domain, target_storage);

    CHECK(source_storage.store_count() == stores_before_copy);
    CHECK(node->link_sync_states.size() == 2);
    CHECK(HasValidLocalSyncEntry(*node));
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

    // Share relationship identity is shared topology: it crosses the network
    // copy unchanged while the local sync state of those relationships does
    // not (the receiver creates its own).
    CHECK(imported->shares[0].share_id == share_a_id);
    CHECK(imported->shares[1].share_id == share_b_id);

    // Local sync: no valid LocalPtr ids / no LinkSyncState objects.
    CHECK(!HasValidLocalSyncEntry(*imported));
    for (auto const& entry : imported->link_sync_states) {
      CHECK(!entry.is_valid());
    }
    CHECK(imported->GetInitialSyncPhase(imported->shares[0].link) ==
          InitialSyncPhase::NotStarted);
    CHECK(imported->GetInitialSyncPhase(imported->shares[1].link) ==
          InitialSyncPhase::NotStarted);

    // Receiver creates its own local sync independently (new share → Events).
    auto link_c = MakeMemoryLink(target_domain, ae::ObjId{54}, "carol");
    imported->AddShare(link_c, ShareAccess::ReadOnly);
    imported->SetInitialSyncPhase(link_c, InitialSyncPhase::Pending);
    CHECK(imported->GetInitialSyncPhase(link_c) == InitialSyncPhase::Pending);
    CHECK(node->GetInitialSyncPhase(link_b) == InitialSyncPhase::Complete);
  }
}

void TestNestedSharedNodeLocalPtrExcluded() {
  ae::RamDomainStorage source_storage;
  ae::ObjId::Type const root_id = 71;
  ae::ObjId::Type const child_id = 72;
  ae::ObjId::Type const link_root_id = 73;
  ae::ObjId::Type const link_child_id = 74;
  ae::ObjId::Type root_sync_id = 0;
  ae::ObjId::Type child_sync_id = 0;
  {
    ae::Domain domain{source_storage};
    auto root =
        RootSharedNode::ptr::Create(ae::CreateWith{domain}.with_id(root_id));
    auto child =
        ChildSharedNode::ptr::Create(ae::CreateWith{domain}.with_id(child_id));
    // Reflected fields before InitializeRuntimeNode (creation-time config).
    child->child_value = 9;
    InitializeRuntimeNode(*child);
    root->child = child;
    root->root_value = 7;
    InitializeRuntimeNode(*root);

    auto link_root = MakeMemoryLink(domain, ae::ObjId{link_root_id}, "root-l");
    auto link_child =
        MakeMemoryLink(domain, ae::ObjId{link_child_id}, "child-l");
    root->AddShare(link_root, ShareAccess::ReadWrite);
    child->AddShare(link_child, ShareAccess::ReadWrite);
    root->SetInitialSyncPhase(link_root, InitialSyncPhase::Complete);
    child->SetInitialSyncPhase(link_child, InitialSyncPhase::Complete);
    root_sync_id = root->link_sync_states[0].id().id();
    child_sync_id = child->link_sync_states[0].id().id();
    root.Save();
    child.Save();
    link_root.Save();
    link_child.Save();
    for (auto& entry : root->link_sync_states) {
      entry.Save();
    }
    for (auto& entry : child->link_sync_states) {
      entry.Save();
    }

    ae::RamDomainStorage target_storage;
    ae::Domain target_domain{target_storage};
    CopySharedNetworkGraph(root, target_domain, target_storage);

    auto imported = RootSharedNode::ptr::Declare(
        ae::CreateWith{target_domain}.with_id(root_id));
    imported.Load();
    CHECK(imported->root_value == 7);
    CHECK(imported->shares.size() == 1);
    imported->shares[0].link.Load();
    CHECK(imported->shares[0].link.id().id() == link_root_id);
    MemoryLink::ptr imported_root_link = imported->shares[0].link;
    imported_root_link.Load();
    CHECK(imported_root_link->endpoint_uid == "root-l");

    CHECK(imported->child.is_valid());
    imported->child.Load();
    CHECK(imported->child->child_value == 9);
    CHECK(imported->child->shares.size() == 1);
    imported->child->shares[0].link.Load();
    CHECK(imported->child->shares[0].link.id().id() == link_child_id);

    CHECK(!HasValidLocalSyncEntry(*imported));
    CHECK(!HasValidLocalSyncEntry(*imported->child));
    for (auto const& entry : imported->link_sync_states) {
      CHECK(!entry.is_valid());
    }
    for (auto const& entry : imported->child->link_sync_states) {
      CHECK(!entry.is_valid());
    }
    CHECK(!StorageHasClass(target_storage, ae::ObjId{root_sync_id},
                           LinkSyncState::kClassId));
    CHECK(!StorageHasClass(target_storage, ae::ObjId{child_sync_id},
                           LinkSyncState::kClassId));
  }
}

void TestGenericLocalPtrNetworkExclusion() {
  ae::RamDomainStorage source_storage;
  ae::ObjId::Type const holder_id = 81;
  ae::ObjId::Type const payload_id = 82;
  {
    ae::Domain domain{source_storage};
    auto holder =
        LocalPtrHolder::ptr::Create(ae::CreateWith{domain}.with_id(holder_id));
    auto payload = LocalOnlyPayload::ptr::Create(
        ae::CreateWith{domain}.with_id(payload_id));
    holder->name = "holder";
    payload->mark = "secret-local";
    holder->local = payload;
    holder.Save();
    payload.Save();

    // Local Save/Load keeps LocalPtr.
    {
      ae::Domain reload{source_storage};
      auto loaded = LocalPtrHolder::ptr::Declare(
          ae::CreateWith{reload}.with_id(holder_id));
      loaded.Load();
      CHECK(loaded->name == "holder");
      CHECK(loaded->local.is_valid());
      loaded->local.Load();
      CHECK(loaded->local->mark == "secret-local");
    }

    ae::RamDomainStorage target_storage;
    CopyNetworkSharedObjectGraph(*holder, target_storage);

    ae::Domain target_domain{target_storage};
    auto imported = LocalPtrHolder::ptr::Declare(
        ae::CreateWith{target_domain}.with_id(holder_id));
    imported.Load();
    CHECK(imported->name == "holder");
    CHECK(!imported->local.is_valid());
    CHECK(imported->local.id().id() == ae::ObjId{}.id());
    CHECK(target_storage.Enumerate(ae::ObjId{payload_id}).empty());
  }
}

void TestLinkSyncStateConfiguredBeforeLive() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto node =
      SharedValueNode::ptr::Create(ae::CreateWith{domain}.with_id(91));
  InitializeRuntimeNode(*node);
  auto link = MakeMemoryLink(domain, ae::ObjId{92}, "sync-peer");

  // AddShare Apply creates LinkSyncState: link + NotStarted assigned before
  // InitializeRuntimeNode, then phase changes only via Event.
  node->AddShare(link, ShareAccess::ReadWrite);
  CHECK(node->link_sync_states.size() == 1);
  auto sync = node->link_sync_states[0];
  CHECK(sync.is_valid());
  sync.Load();
  CHECK(sync->base.is_valid());  // live after AddShare
  CHECK(sync->link.is_valid());
  CHECK(sync->link.id() == link.id());
  CHECK(sync->share_id == node->shares[0].share_id);
  CHECK(sync->GetInitialSyncPhase() == InitialSyncPhase::NotStarted);

  node->SetInitialSyncPhase(link, InitialSyncPhase::Complete);
  CHECK(sync->GetInitialSyncPhase() == InitialSyncPhase::Complete);
  CHECK(sync->journal.size() >= 1);
}

void TestSimultaneousDomainGraphSerializationScopes() {
  // Two DomainGraph lifetimes with different scopes must not interfere —
  // scope is owned by each DomainGraph, not a process-global registry.
  ae::RamDomainStorage source_storage;
  ae::ObjId::Type const holder_id = 101;
  ae::ObjId::Type const payload_id = 102;
  ae::Domain source_domain{source_storage};
  auto holder =
      LocalPtrHolder::ptr::Create(ae::CreateWith{source_domain}.with_id(holder_id));
  auto payload = LocalOnlyPayload::ptr::Create(
      ae::CreateWith{source_domain}.with_id(payload_id));
  holder->name = "dual-scope";
  payload->mark = "local-only";
  holder->local = payload;

  auto ptr = source_domain.Find(holder.id());
  CHECK(ptr);
  auto* factory =
      ae::Registry::GetRegistry().FindFactory(holder->GetClassId());
  CHECK(factory != nullptr);
  CHECK(factory->save != nullptr);

  ae::RamDomainStorage local_out;
  ae::RamDomainStorage network_out;
  std::exception_ptr local_err;
  std::exception_ptr network_err;

  std::thread local_thread{[&] {
    try {
      ae::Domain local_domain{local_out};
      ae::DomainGraph local_graph{
          &local_domain, ae::GraphSerializationScope::LocalPersistent};
      CHECK(local_graph.serialization_scope ==
            ae::GraphSerializationScope::LocalPersistent);
      factory->save(&local_graph, ptr, holder.id());
    } catch (...) {
      local_err = std::current_exception();
    }
  }};

  std::thread network_thread{[&] {
    try {
      ae::Domain network_domain{network_out};
      ae::DomainGraph network_graph{
          &network_domain, ae::GraphSerializationScope::NetworkShared};
      CHECK(network_graph.serialization_scope ==
            ae::GraphSerializationScope::NetworkShared);
      factory->save(&network_graph, ptr, holder.id());
    } catch (...) {
      network_err = std::current_exception();
    }
  }};

  local_thread.join();
  network_thread.join();
  CHECK(!local_err);
  CHECK(!network_err);

  {
    ae::Domain d{local_out};
    auto loaded =
        LocalPtrHolder::ptr::Declare(ae::CreateWith{d}.with_id(holder_id));
    loaded.Load();
    CHECK(loaded->name == "dual-scope");
    CHECK(loaded->local.is_valid());
    loaded->local.Load();
    CHECK(loaded->local->mark == "local-only");
  }
  {
    ae::Domain d{network_out};
    auto loaded =
        LocalPtrHolder::ptr::Declare(ae::CreateWith{d}.with_id(holder_id));
    loaded.Load();
    CHECK(loaded->name == "dual-scope");
    CHECK(!loaded->local.is_valid());
    CHECK(network_out.Enumerate(ae::ObjId{payload_id}).empty());
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
  apptraverse::test::TestLinkConfigInitializedBeforeLive();
  apptraverse::test::TestMultipleRefsSameLinkAfterRestart();
  apptraverse::test::TestShareTopologyEventsAndPersistence();
  apptraverse::test::TestLocalSyncStateEventDrivenSaveLoadAndReplay();
  apptraverse::test::TestRemoveShareAddShareResetsLocalSync();
  apptraverse::test::TestShareRelationshipIdentitySurvivesForcedReplay();
  apptraverse::test::TestNetworkSerializationBoundaries();
  apptraverse::test::TestNestedSharedNodeLocalPtrExcluded();
  apptraverse::test::TestGenericLocalPtrNetworkExclusion();
  apptraverse::test::TestLinkSyncStateConfiguredBeforeLive();
  apptraverse::test::TestSimultaneousDomainGraphSerializationScopes();
  apptraverse::test::TestNoSerializedIsLocalAndNoRttiSurface();

  std::cout << "shared_node_foundation_test OK\n";
  return 0;
}
