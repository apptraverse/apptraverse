#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
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
using apptraverse::test::kSyncBaseSnapshotId;
using apptraverse::test::kSyncNodeId;

struct Replica {
  explicit Replica(std::filesystem::path writable,
                   std::filesystem::path const& base)
      : storage{std::move(writable), {base}},
        domain{ae::Now(), storage},
        node{SyncNode::ptr::Declare(
            ae::CreateWith{domain}.with_id(kSyncNodeId))} {
    node.Load();
    APPTRAVERSE_CHECK(node);
  }

  apptraverse::VersionedDirectoryStorage storage;
  ae::Domain domain;
  SyncNode::ptr node;
  MemoryEventTransport transport;
};

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
    a.node->InitializeReplicaForTest(kReplicaA);
    APPTRAVERSE_CHECK(a.node->AppendToken(ae::ObjId{kSyncBaseSnapshotId},
                                          ae::ObjId{100}, std::int32_t{11}));
    APPTRAVERSE_CHECK(a.node->AppendToken(ae::ObjId{kSyncBaseSnapshotId},
                                          ae::ObjId{101}, std::int32_t{12}));
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
    APPTRAVERSE_CHECK(a.node->next_local_sequence_for_test() == 3);
    APPTRAVERSE_CHECK(a.node->logical_clock_for_test() == 2);
    APPTRAVERSE_CHECK(a.node->AppendToken(ae::ObjId{kSyncBaseSnapshotId},
                                          ae::ObjId{102}, std::int32_t{13}));
    APPTRAVERSE_CHECK(
        (a.node->journal_identity_at_for_test(2) == EventIdentity(kReplicaA, 3)));
    APPTRAVERSE_CHECK(a.node->journal_logical_time_at_for_test(2) == 3);
    a.node.Save();
  }

  // Fresh pair for sync tests
  std::filesystem::remove_all(writable_a);
  std::filesystem::remove_all(writable_b);

  // Tests 2-5: one-way, reverse, concurrent, duplicate
  {
    Replica a{writable_a, base_root};
    Replica b{writable_b, base_root};
    a.node->InitializeReplicaForTest(kReplicaA);
    b.node->InitializeReplicaForTest(kReplicaB);

    a.transport.Connect(b.transport);
    JournalSynchronizer sync_a{*a.node, a.domain, a.storage, a.transport};
    JournalSynchronizer sync_b{*b.node, b.domain, b.storage, b.transport};

    // Test 2: one-way A -> B
    APPTRAVERSE_CHECK(a.node->AppendToken(ae::ObjId{kSyncBaseSnapshotId},
                                          ae::ObjId{200}, std::int32_t{1}));
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

    // Test 3: reverse B -> A
    auto const received_time = b.node->journal_logical_time_at_for_test(0);
    APPTRAVERSE_CHECK(b.node->AppendToken(ae::ObjId{kSyncBaseSnapshotId},
                                          ae::ObjId{201}, std::int32_t{2}));
    APPTRAVERSE_CHECK(b.node->journal_logical_time_at_for_test(1) >
                      received_time);
    sync_b.FlushPending();
    ExpectSameJournalOrder(a.node, b.node);
    ExpectSameTokens(a.node, b.node);
    APPTRAVERSE_CHECK(a.node->tokens() == std::vector<std::int32_t>({1, 2}));

    // Test 5: duplicate delivery of A's first event to B
    auto const applies_before = b.node->apply_calls_for_test();
    auto const size_before = b.node->journal_size_for_test();
    b.transport.RedeliverLastEvent();
    APPTRAVERSE_CHECK(b.node->journal_size_for_test() == size_before);
    APPTRAVERSE_CHECK(b.node->apply_calls_for_test() == applies_before);

    a.node.Save();
    b.node.Save();
  }

  // Test 4: concurrent events with reversed delivery
  std::filesystem::remove_all(writable_a);
  std::filesystem::remove_all(writable_b);
  {
    Replica a{writable_a, base_root};
    Replica b{writable_b, base_root};
    a.node->InitializeReplicaForTest(kReplicaA);
    b.node->InitializeReplicaForTest(kReplicaB);
    a.transport.Connect(b.transport);
    a.transport.SetMode(MemoryTransportMode::kQueued);
    b.transport.SetMode(MemoryTransportMode::kQueued);
    JournalSynchronizer sync_a{*a.node, a.domain, a.storage, a.transport};
    JournalSynchronizer sync_b{*b.node, b.domain, b.storage, b.transport};

    APPTRAVERSE_CHECK(a.node->AppendToken(ae::ObjId{kSyncBaseSnapshotId},
                                          ae::ObjId{300}, std::int32_t{10}));
    APPTRAVERSE_CHECK(b.node->AppendToken(ae::ObjId{kSyncBaseSnapshotId},
                                          ae::ObjId{301}, std::int32_t{20}));
    APPTRAVERSE_CHECK(a.node->journal_logical_time_at_for_test(0) == 1);
    APPTRAVERSE_CHECK(b.node->journal_logical_time_at_for_test(0) == 1);

    sync_a.FlushPending();
    sync_b.FlushPending();
    APPTRAVERSE_CHECK(a.transport.queued_event_count() == 1);
    APPTRAVERSE_CHECK(b.transport.queued_event_count() == 1);

    // Deliver in opposite orders.
    a.transport.DeliverQueuedEventsInOrder();
    b.transport.DeliverQueuedEventsInOrder();
    a.transport.DeliverQueuedConfirmations();
    b.transport.DeliverQueuedConfirmations();

    ExpectSameJournalOrder(a.node, b.node);
    ExpectSameTokens(a.node, b.node);
    // Equal logical time: lower replica id first => A then B.
    APPTRAVERSE_CHECK(a.node->journal_identity_at_for_test(0).origin ==
                      kReplicaA);
    APPTRAVERSE_CHECK(a.node->journal_identity_at_for_test(1).origin ==
                      kReplicaB);
    APPTRAVERSE_CHECK(a.node->tokens() == std::vector<std::int32_t>({10, 20}));

    a.node.Save();
    b.node.Save();
  }

  // Concurrent with reversed queue delivery
  std::filesystem::remove_all(writable_a);
  std::filesystem::remove_all(writable_b);
  {
    Replica a{writable_a, base_root};
    Replica b{writable_b, base_root};
    a.node->InitializeReplicaForTest(kReplicaA);
    b.node->InitializeReplicaForTest(kReplicaB);
    a.transport.Connect(b.transport);
    a.transport.SetMode(MemoryTransportMode::kQueued);
    b.transport.SetMode(MemoryTransportMode::kQueued);
    JournalSynchronizer sync_a{*a.node, a.domain, a.storage, a.transport};
    JournalSynchronizer sync_b{*b.node, b.domain, b.storage, b.transport};

    APPTRAVERSE_CHECK(a.node->AppendToken(ae::ObjId{kSyncBaseSnapshotId},
                                          ae::ObjId{400}, std::int32_t{10}));
    APPTRAVERSE_CHECK(b.node->AppendToken(ae::ObjId{kSyncBaseSnapshotId},
                                          ae::ObjId{401}, std::int32_t{20}));
    sync_a.FlushPending();
    sync_b.FlushPending();

    // Reverse delivery into each peer.
    a.transport.DeliverQueuedEventsReversed();
    b.transport.DeliverQueuedEventsReversed();
    a.transport.DeliverQueuedConfirmations();
    b.transport.DeliverQueuedConfirmations();

    ExpectSameJournalOrder(a.node, b.node);
    ExpectSameTokens(a.node, b.node);
    APPTRAVERSE_CHECK(a.node->tokens() == std::vector<std::int32_t>({10, 20}));

    a.node.Save();
    b.node.Save();
  }

  // Test 6: persistence after bidirectional sync
  {
    Replica a{writable_a, base_root};
    Replica b{writable_b, base_root};
    APPTRAVERSE_CHECK(a.node->replica_id_for_test() == kReplicaA);
    APPTRAVERSE_CHECK(b.node->replica_id_for_test() == kReplicaB);
    APPTRAVERSE_CHECK(a.node->next_local_sequence_for_test() == 2);
    APPTRAVERSE_CHECK(b.node->next_local_sequence_for_test() == 2);
    ExpectSameJournalOrder(a.node, b.node);
    ExpectSameTokens(a.node, b.node);
    APPTRAVERSE_CHECK(a.node->tokens() == std::vector<std::int32_t>({10, 20}));
    APPTRAVERSE_CHECK(a.node->journal_delivery_state_at_for_test(0) ==
                      EventDeliveryState::kConfirmed);
    APPTRAVERSE_CHECK(a.node->journal_delivery_state_at_for_test(1) ==
                      EventDeliveryState::kConfirmed);
  }

  return 0;
}
