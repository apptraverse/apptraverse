#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "aether/obj/domain.h"

#include "apptraverse/event_record.h"
#include "apptraverse/journal_synchronizer.h"
#include "apptraverse/versioned_directory_storage.h"

#include "journal_sync_fixture.h"
#include "memory_event_transport.h"

namespace {

using apptraverse::EventDeliveryState;
using apptraverse::EventIdentity;
using apptraverse::JournalSynchronizer;
using apptraverse::test::MemoryEventTransport;
using apptraverse::test::MemoryTransportMode;
using apptraverse::test::SyncNode;
using apptraverse::test::kReplicaA;
using apptraverse::test::kReplicaB;
using apptraverse::test::kSnapshotA;
using apptraverse::test::kSnapshotB;
using apptraverse::test::kSyncNodeId;

struct Replica {
  explicit Replica(std::filesystem::path writable,
                   std::filesystem::path const& base)
      : writable_root{std::move(writable)},
        storage{writable_root, {base}},
        domain{ae::Now(), storage},
        node{SyncNode::ptr::Declare(
            ae::CreateWith{domain}.with_id(kSyncNodeId))} {
    node.Load();
    APPTRAVERSE_CHECK(node);
  }

  std::filesystem::path writable_root;
  apptraverse::VersionedDirectoryStorage storage;
  ae::Domain domain;
  SyncNode::ptr node;
  MemoryEventTransport transport;
};

std::set<std::string> ListTopLevelObjectDirs(
    std::filesystem::path const& root) {
  std::set<std::string> names;
  std::error_code ec;
  for (auto const& entry : std::filesystem::directory_iterator{root, ec}) {
    if (ec) {
      break;
    }
    std::error_code entry_ec;
    if (!entry.is_directory(entry_ec) || entry_ec) {
      continue;
    }
    names.insert(entry.path().filename().string());
  }
  return names;
}

void ExpectSameTokens(SyncNode::ptr const& a, SyncNode::ptr const& b) {
  APPTRAVERSE_CHECK(a->tokens() == b->tokens());
}

void ExpectSameJournalOrder(SyncNode::ptr const& a, SyncNode::ptr const& b) {
  APPTRAVERSE_CHECK(a->journal_size_for_test() == b->journal_size_for_test());
  for (std::size_t i = 0; i < a->journal_size_for_test(); ++i) {
    APPTRAVERSE_CHECK(a->journal_identity_at_for_test(i) ==
                      b->journal_identity_at_for_test(i));
    APPTRAVERSE_CHECK(a->journal_logical_time_at_for_test(i) ==
                      b->journal_logical_time_at_for_test(i));
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "usage: apptraverse_journal_sync_runtime "
                 "<writable-a> <writable-b> <base-root>\n";
    return 1;
  }

  std::filesystem::path const writable_a{argv[1]};
  std::filesystem::path const writable_b{argv[2]};
  std::filesystem::path const base_root{argv[3]};

  // Test 1: local sequence persistence
  {
    Replica a{writable_a, base_root};
    a.node->InitializeReplicaForTest(kReplicaA, ae::ObjId{kSnapshotA});
    APPTRAVERSE_CHECK(a.node->base_snapshot_id_for_test().id() == kSnapshotA);
    APPTRAVERSE_CHECK(a.node->AppendToken(ae::ObjId{100}, std::int32_t{11}));
    APPTRAVERSE_CHECK(a.node->AppendToken(ae::ObjId{101}, std::int32_t{12}));
    APPTRAVERSE_CHECK(a.node->journal_size_for_test() == 2);
    APPTRAVERSE_CHECK(
        (a.node->journal_identity_at_for_test(0) == EventIdentity(kReplicaA, 1)));
    APPTRAVERSE_CHECK(
        (a.node->journal_identity_at_for_test(1) == EventIdentity(kReplicaA, 2)));
    APPTRAVERSE_CHECK(a.node->journal_logical_time_at_for_test(0) == 1);
    APPTRAVERSE_CHECK(a.node->journal_logical_time_at_for_test(1) == 2);
    APPTRAVERSE_CHECK(a.node->next_local_sequence_for_test() == 3);
    a.node.Save();
  }
  {
    Replica a{writable_a, base_root};
    APPTRAVERSE_CHECK(a.node->replica_id_for_test() == kReplicaA);
    APPTRAVERSE_CHECK(a.node->base_snapshot_id_for_test().id() == kSnapshotA);
    APPTRAVERSE_CHECK(a.node->next_local_sequence_for_test() == 3);
    APPTRAVERSE_CHECK(a.node->logical_clock_for_test() == 2);
    APPTRAVERSE_CHECK(a.node->AppendToken(ae::ObjId{102}, std::int32_t{13}));
    APPTRAVERSE_CHECK(
        (a.node->journal_identity_at_for_test(2) == EventIdentity(kReplicaA, 3)));
    APPTRAVERSE_CHECK(a.node->journal_logical_time_at_for_test(2) == 3);
    a.node.Save();
  }

  std::filesystem::remove_all(writable_a);
  std::filesystem::remove_all(writable_b);

  // Test 2 + 3 + duplicate storage: one-way then reverse, then duplicate
  {
    Replica a{writable_a, base_root};
    Replica b{writable_b, base_root};
    a.node->InitializeReplicaForTest(kReplicaA, ae::ObjId{kSnapshotA});
    b.node->InitializeReplicaForTest(kReplicaB, ae::ObjId{kSnapshotB});

    a.transport.Connect(b.transport);
    JournalSynchronizer sync_a{*a.node, a.domain, a.storage, a.transport};
    JournalSynchronizer sync_b{*b.node, b.domain, b.storage, b.transport};

    APPTRAVERSE_CHECK(a.node->AppendToken(ae::ObjId{210}, std::int32_t{1}));
    APPTRAVERSE_CHECK(a.node->journal_delivery_state_at_for_test(0) ==
                      EventDeliveryState::kPending);
    sync_a.FlushPending();
    APPTRAVERSE_CHECK(a.node->journal_delivery_state_at_for_test(0) ==
                      EventDeliveryState::kConfirmed);
    APPTRAVERSE_CHECK(b.node->journal_size_for_test() == 1);
    APPTRAVERSE_CHECK(
        (b.node->journal_identity_at_for_test(0) == EventIdentity(kReplicaA, 1)));
    APPTRAVERSE_CHECK(b.node->journal_delivery_state_at_for_test(0) ==
                      EventDeliveryState::kConfirmed);
    ExpectSameTokens(a.node, b.node);
    APPTRAVERSE_CHECK(a.node->tokens() == std::vector<std::int32_t>({1}));

    auto const received_time = b.node->journal_logical_time_at_for_test(0);
    APPTRAVERSE_CHECK(b.node->AppendToken(ae::ObjId{211}, std::int32_t{2}));
    APPTRAVERSE_CHECK(b.node->journal_logical_time_at_for_test(1) >
                      received_time);
    sync_b.FlushPending();
    ExpectSameJournalOrder(a.node, b.node);
    ExpectSameTokens(a.node, b.node);
    APPTRAVERSE_CHECK(a.node->tokens() == std::vector<std::int32_t>({1, 2}));

    auto const objects_before = ListTopLevelObjectDirs(b.writable_root);
    auto const applies_before = b.node->apply_calls_for_test();
    auto const size_before = b.node->journal_size_for_test();
    auto const confirmations_before = b.transport.confirmation_send_count();
    b.transport.RedeliverLastEvent();
    APPTRAVERSE_CHECK(b.node->journal_size_for_test() == size_before);
    APPTRAVERSE_CHECK(b.node->apply_calls_for_test() == applies_before);
    APPTRAVERSE_CHECK(b.node->tokens() == std::vector<std::int32_t>({1, 2}));
    APPTRAVERSE_CHECK(ListTopLevelObjectDirs(b.writable_root) == objects_before);
    APPTRAVERSE_CHECK(b.transport.confirmation_send_count() ==
                      confirmations_before + 1);

    a.node.Save();
    b.node.Save();
  }

  std::filesystem::remove_all(writable_a);
  std::filesystem::remove_all(writable_b);

  // Test 4: concurrent events
  {
    Replica a{writable_a, base_root};
    Replica b{writable_b, base_root};
    a.node->InitializeReplicaForTest(kReplicaA, ae::ObjId{kSnapshotA});
    b.node->InitializeReplicaForTest(kReplicaB, ae::ObjId{kSnapshotB});
    a.transport.Connect(b.transport);
    a.transport.SetMode(MemoryTransportMode::kQueued);
    b.transport.SetMode(MemoryTransportMode::kQueued);
    JournalSynchronizer sync_a{*a.node, a.domain, a.storage, a.transport};
    JournalSynchronizer sync_b{*b.node, b.domain, b.storage, b.transport};

    APPTRAVERSE_CHECK(a.node->AppendToken(ae::ObjId{300}, std::int32_t{10}));
    APPTRAVERSE_CHECK(b.node->AppendToken(ae::ObjId{301}, std::int32_t{20}));
    APPTRAVERSE_CHECK(a.node->journal_logical_time_at_for_test(0) == 1);
    APPTRAVERSE_CHECK(b.node->journal_logical_time_at_for_test(0) == 1);

    sync_a.FlushPending();
    sync_b.FlushPending();
    APPTRAVERSE_CHECK(a.transport.queued_event_count() == 1);
    APPTRAVERSE_CHECK(b.transport.queued_event_count() == 1);

    a.transport.DeliverQueuedEventsInOrder();
    b.transport.DeliverQueuedEventsInOrder();
    a.transport.DeliverQueuedConfirmations();
    b.transport.DeliverQueuedConfirmations();

    ExpectSameJournalOrder(a.node, b.node);
    ExpectSameTokens(a.node, b.node);
    APPTRAVERSE_CHECK(a.node->journal_identity_at_for_test(0).origin ==
                      kReplicaA);
    APPTRAVERSE_CHECK(a.node->journal_identity_at_for_test(1).origin ==
                      kReplicaB);
    APPTRAVERSE_CHECK(a.node->tokens() == std::vector<std::int32_t>({10, 20}));
  }

  std::filesystem::remove_all(writable_a);
  std::filesystem::remove_all(writable_b);

  // Reversed delivery of TWO remote events from A to B
  {
    Replica a{writable_a, base_root};
    Replica b{writable_b, base_root};
    a.node->InitializeReplicaForTest(kReplicaA, ae::ObjId{kSnapshotA});
    b.node->InitializeReplicaForTest(kReplicaB, ae::ObjId{kSnapshotB});
    a.transport.Connect(b.transport);
    a.transport.SetMode(MemoryTransportMode::kQueued);
    b.transport.SetMode(MemoryTransportMode::kQueued);
    JournalSynchronizer sync_a{*a.node, a.domain, a.storage, a.transport};
    JournalSynchronizer sync_b{*b.node, b.domain, b.storage, b.transport};

    APPTRAVERSE_CHECK(a.node->AppendToken(ae::ObjId{400}, std::int32_t{41}));
    APPTRAVERSE_CHECK(a.node->AppendToken(ae::ObjId{401}, std::int32_t{42}));
    APPTRAVERSE_CHECK(a.node->journal_logical_time_at_for_test(0) == 1);
    APPTRAVERSE_CHECK(a.node->journal_logical_time_at_for_test(1) == 2);
    sync_a.FlushPending();
    APPTRAVERSE_CHECK(a.transport.queued_event_count() == 2);

    a.transport.DeliverQueuedEventsReversed();
    a.transport.DeliverQueuedConfirmations();

    APPTRAVERSE_CHECK(b.node->journal_size_for_test() == 2);
    APPTRAVERSE_CHECK(
        (b.node->journal_identity_at_for_test(0) == EventIdentity(kReplicaA, 1)));
    APPTRAVERSE_CHECK(
        (b.node->journal_identity_at_for_test(1) == EventIdentity(kReplicaA, 2)));
    APPTRAVERSE_CHECK(b.node->journal_logical_time_at_for_test(0) == 1);
    APPTRAVERSE_CHECK(b.node->journal_logical_time_at_for_test(1) == 2);
    APPTRAVERSE_CHECK(b.node->tokens() == std::vector<std::int32_t>({41, 42}));
    APPTRAVERSE_CHECK(b.node->apply_calls_for_test() == 3);
    // After A2 alone: 1 apply; after rebuild with A1+A2: 2 more applies => 3.
    ExpectSameJournalOrder(a.node, b.node);
    ExpectSameTokens(a.node, b.node);
  }

  std::filesystem::remove_all(writable_a);
  std::filesystem::remove_all(writable_b);

  // Remote-first, then local, then save/reload
  {
    Replica a{writable_a, base_root};
    Replica b{writable_b, base_root};
    a.node->InitializeReplicaForTest(kReplicaA, ae::ObjId{kSnapshotA});
    b.node->InitializeReplicaForTest(kReplicaB, ae::ObjId{kSnapshotB});
    APPTRAVERSE_CHECK(b.node->base_snapshot_id_for_test().id() == kSnapshotB);
    APPTRAVERSE_CHECK(b.node->journal_size_for_test() == 0);

    a.transport.Connect(b.transport);
    JournalSynchronizer sync_a{*a.node, a.domain, a.storage, a.transport};
    JournalSynchronizer sync_b{*b.node, b.domain, b.storage, b.transport};

    APPTRAVERSE_CHECK(a.node->AppendToken(ae::ObjId{500}, std::int32_t{7}));
    sync_a.FlushPending();
    APPTRAVERSE_CHECK(b.node->journal_size_for_test() == 1);
    APPTRAVERSE_CHECK(
        (b.node->journal_identity_at_for_test(0) == EventIdentity(kReplicaA, 1)));
    APPTRAVERSE_CHECK(b.node->tokens() == std::vector<std::int32_t>({7}));
    APPTRAVERSE_CHECK(b.node->apply_calls_for_test() == 1);

    APPTRAVERSE_CHECK(b.node->AppendToken(ae::ObjId{501}, std::int32_t{8}));
    APPTRAVERSE_CHECK(b.node->journal_size_for_test() == 2);
    APPTRAVERSE_CHECK(b.node->tokens() == std::vector<std::int32_t>({7, 8}));
    sync_b.FlushPending();

    ExpectSameJournalOrder(a.node, b.node);
    ExpectSameTokens(a.node, b.node);
    APPTRAVERSE_CHECK(a.node->tokens() == std::vector<std::int32_t>({7, 8}));

    a.node.Save();
    b.node.Save();
  }
  {
    Replica a{writable_a, base_root};
    Replica b{writable_b, base_root};
    APPTRAVERSE_CHECK(a.node->replica_id_for_test() == kReplicaA);
    APPTRAVERSE_CHECK(b.node->replica_id_for_test() == kReplicaB);
    APPTRAVERSE_CHECK(a.node->base_snapshot_id_for_test().id() == kSnapshotA);
    APPTRAVERSE_CHECK(b.node->base_snapshot_id_for_test().id() == kSnapshotB);
    APPTRAVERSE_CHECK(a.node->journal_size_for_test() == 2);
    APPTRAVERSE_CHECK(b.node->journal_size_for_test() == 2);
    ExpectSameJournalOrder(a.node, b.node);
    ExpectSameTokens(a.node, b.node);
    APPTRAVERSE_CHECK(a.node->tokens() == std::vector<std::int32_t>({7, 8}));
    APPTRAVERSE_CHECK(b.node->tokens() == std::vector<std::int32_t>({7, 8}));
    // Reload replays the journal once per event.
    APPTRAVERSE_CHECK(a.node->apply_calls_for_test() == 2);
    APPTRAVERSE_CHECK(b.node->apply_calls_for_test() == 2);
  }

  return 0;
}
