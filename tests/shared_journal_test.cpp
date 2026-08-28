#include <cassert>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

#include "apptraverse/shared_runtime.h"

#include "chat_bootstrap.h"
#include "chat_commands.h"
#include "chat_model.h"
#include "chat_model.h"
#include "chat_shared.h"

namespace {

using namespace apptraverse;

void test_ack_clears_pending() {
  SharedRuntime runtime;
  SharedInstance<ChatRoom> instance;
  instance.local_aether_uid = "alice";
  instance.shared_room_id = "room";
  auto& peer = runtime.EnsurePeer(instance, "bob");
  SharedEventId e1{.origin_uid = "alice", .origin_sequence = 1};
  SharedEventId e2{.origin_uid = "alice", .origin_sequence = 2};
  peer.pending.push_back(e1);
  peer.pending.push_back(e2);
  peer.in_flight = e1;
  runtime.OnAckReceived(instance, "bob", e1);
  assert(!peer.in_flight.has_value());
  assert(peer.pending.size() == 1);
  assert(peer.pending.front() == e2);
}

void test_duplicate_event_id() {
  SharedRuntime runtime;
  SharedInstance<ChatRoom> instance;
  instance.local_aether_uid = "alice";
  SharedEventId id{.origin_uid = "alice", .origin_sequence = 1};
  SharedEventOrder order{.lamport = 1,
                         .origin_uid = "alice",
                         .origin_sequence = 1};
  runtime.OnIncomingEventApplied(
      instance, id, order, "bob",
      [](PeerDeliveryState&, SharedEventId const&) {});
  assert(instance.HasSharedEvent(id));
  runtime.OnIncomingEventApplied(
      instance, id, order, "bob",
      [](PeerDeliveryState&, SharedEventId const&) {});
  assert(instance.shared_journal.size() == 1);
}

void test_seed_pending_excludes_peer_origin() {
  SharedRuntime runtime;
  SharedInstance<ChatRoom> instance;
  instance.local_aether_uid = "alice";
  instance.shared_journal = {
      SharedJournalEntry{
          .id = {.origin_uid = "alice", .origin_sequence = 1},
          .order = {.lamport = 1, .origin_uid = "alice", .origin_sequence = 1}},
      SharedJournalEntry{
          .id = {.origin_uid = "bob", .origin_sequence = 1},
          .order = {.lamport = 2, .origin_uid = "bob", .origin_sequence = 1}},
  };
  for (auto const& entry : instance.shared_journal) {
    instance.RememberSharedEvent(entry.id);
  }
  auto& peer = runtime.EnsurePeer(instance, "bob");
  runtime.SeedPendingFromJournal(instance, peer);
  assert(peer.pending.size() == 1);
  assert(peer.pending.front().origin_uid == "alice");
}

void test_local_commit_enqueues_other_peers() {
  SharedRuntime runtime;
  SharedInstance<ChatRoom> instance;
  instance.local_aether_uid = "alice";
  runtime.EnsurePeer(instance, "bob");
  SharedEventId id{.origin_uid = "alice", .origin_sequence = 1};
  runtime.OnLocalEventCommitted(
      instance, id,
      [](PeerDeliveryState& peer, SharedEventId const& event_id) {
        peer.pending.push_back(event_id);
      });
  assert(instance.peers.front().pending.size() == 1);
}

ChatRuntime MakeRuntime(std::filesystem::path const& dir) {
  EnsureChatRegistration();
  if (!std::filesystem::exists(dir)) {
    DistillChatModel(dir, "Peer");
  }
  return LoadChatModel(dir);
}

void test_preconnect_local_journal() {
  auto const dir =
      std::filesystem::temp_directory_path() / "apptraverse-shared-journal-a";
  auto runtime = MakeRuntime(dir);
  ChatSharedBinding binding;
  binding.runtime = SharedRuntime{};
  InitializeChatSharedBinding(binding, *runtime.application, "alice-uid");
  CommitLocalJoin(binding, *runtime.application->host_client);
  CommitLocalMessage(binding, *runtime.application->host_client, "before");
  assert(runtime.application->chat_room->feed.size() == 2);
  assert(binding.instance.shared_journal.size() == 2);
}

}  // namespace

int main() {
  test_ack_clears_pending();
  test_duplicate_event_id();
  test_seed_pending_excludes_peer_origin();
  test_local_commit_enqueues_other_peers();
  test_preconnect_local_journal();
  std::cout << "shared_journal_test ok\n";
  return 0;
}
