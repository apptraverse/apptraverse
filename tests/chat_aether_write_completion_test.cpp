// Exercises ChatAetherRuntime::ApplyWriteStatus — the same completion path
// used by production try_start_write status callbacks.

#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "aether/write_action/write_action.h"
#include "chat_aether_runtime.h"

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"         \
                << __LINE__ << '\n';                                           \
      std::exit(1);                                                            \
    }                                                                          \
  } while (0)

namespace {

using apptraverse::example::chat_demo::ChatAetherWriteCompletionTestAccess;
using PeerState = ChatAetherWriteCompletionTestAccess::PeerState;

void ArmActiveWrite(PeerState& peer, std::uint64_t incarnation,
                    std::uint64_t token) {
  peer.channel_incarnation = incarnation;
  peer.active_write_token = token;
  peer.terminal_notice_pending = false;
  peer.terminal_notice.reset();
}

bool Settled(PeerState const& peer, std::uint64_t token,
             std::uint64_t incarnation, ae::WriteAction::Status status) {
  return peer.terminal_notice_pending && peer.terminal_notice.has_value() &&
         peer.terminal_notice->token == token &&
         peer.terminal_notice->incarnation == incarnation &&
         peer.terminal_notice->status == status;
}

bool Pending(PeerState const& peer) {
  return !peer.terminal_notice_pending && !peer.terminal_notice.has_value();
}

void TestTwoPeersSameTokenIncarnationSettleIndependently() {
  std::unordered_map<std::string, PeerState> peers;
  peers.emplace("a", PeerState{});
  peers.emplace("b", PeerState{});
  PeerState* a = &peers.at("a");
  PeerState* b = &peers.at("b");
  ArmActiveWrite(*a, /*incarnation=*/1, /*token=*/1);
  ArmActiveWrite(*b, /*incarnation=*/1, /*token=*/1);

  ChatAetherWriteCompletionTestAccess::ApplyWriteStatus(
      b, 1, 1, ae::WriteAction::Status::kSuccess);
  CHECK(Settled(*b, 1, 1, ae::WriteAction::Status::kSuccess));
  CHECK(Pending(*a));

  ChatAetherWriteCompletionTestAccess::ApplyWriteStatus(
      a, 1, 1, ae::WriteAction::Status::kFail);
  CHECK(Settled(*a, 1, 1, ae::WriteAction::Status::kFail));
  CHECK(Settled(*b, 1, 1, ae::WriteAction::Status::kSuccess));
}

void TestStaleIncarnationIgnored() {
  PeerState peer;
  ArmActiveWrite(peer, /*incarnation=*/2, /*token=*/1);
  ChatAetherWriteCompletionTestAccess::ApplyWriteStatus(
      &peer, 1, /*incarnation=*/1, ae::WriteAction::Status::kSuccess);
  CHECK(Pending(peer));
  CHECK(peer.active_write_token == 1);
  CHECK(peer.channel_incarnation == 2);

  ChatAetherWriteCompletionTestAccess::ApplyWriteStatus(
      &peer, 1, 2, ae::WriteAction::Status::kSuccess);
  CHECK(Settled(peer, 1, 2, ae::WriteAction::Status::kSuccess));
}

void TestStaleTokenIgnored() {
  PeerState peer;
  ArmActiveWrite(peer, /*incarnation=*/1, /*token=*/5);
  ChatAetherWriteCompletionTestAccess::ApplyWriteStatus(
      &peer, /*token=*/4, 1, ae::WriteAction::Status::kSuccess);
  CHECK(Pending(peer));
  ChatAetherWriteCompletionTestAccess::ApplyWriteStatus(
      &peer, 5, 1, ae::WriteAction::Status::kStop);
  CHECK(Settled(peer, 5, 1, ae::WriteAction::Status::kStop));
}

void TestRehashDoesNotChangeCallbackOwnership() {
  std::unordered_map<std::string, PeerState> peers;
  peers.emplace("owner", PeerState{});
  PeerState* owner = &peers.at("owner");
  ArmActiveWrite(*owner, 1, 1);

  // Force rehash while owner pointer remains the production callback context.
  peers.reserve(256);
  for (int i = 0; i < 200; ++i) {
    peers.emplace("pad-" + std::to_string(i), PeerState{});
  }
  CHECK(&peers.at("owner") == owner);

  ChatAetherWriteCompletionTestAccess::ApplyWriteStatus(
      owner, 1, 1, ae::WriteAction::Status::kSuccess);
  CHECK(Settled(*owner, 1, 1, ae::WriteAction::Status::kSuccess));
}

void TestDestroyedChannelStateIgnored() {
  PeerState peer;
  ArmActiveWrite(peer, /*incarnation=*/1, /*token=*/1);
  // Teardown bumps incarnation and clears the active token before any late
  // status can legally settle this channel.
  peer.active_write_token = 0;
  ++peer.channel_incarnation;
  ChatAetherWriteCompletionTestAccess::ApplyWriteStatus(
      &peer, 1, 1, ae::WriteAction::Status::kSuccess);
  CHECK(Pending(peer));
  CHECK(peer.active_write_token == 0);
  CHECK(peer.channel_incarnation == 2);
}

void TestCaptureActiveWriteToPendingPreservesInFlight() {
  using PendingOut = ChatAetherWriteCompletionTestAccess::PendingOut;
  PeerState peer;
  peer.active_write_token = 7;
  peer.active_kind = apptraverse::example::chat_demo::AetherFrameKind::kApplication;
  peer.active_payload = {0x11, 0x22, 0x33};
  peer.pending_out.push_back(
      PendingOut{.kind = apptraverse::example::chat_demo::AetherFrameKind::kControl,
                 .bytes = {0x99}});

  ChatAetherWriteCompletionTestAccess::CaptureActiveWriteToPending(peer);
  CHECK(peer.active_payload.empty());
  CHECK(peer.pending_out.size() == 2);
  CHECK(peer.pending_out.front().bytes.size() == 3);
  CHECK(peer.pending_out.front().bytes[0] == 0x11);
  CHECK(peer.pending_out.back().bytes.size() == 1);
  CHECK(peer.pending_out.back().bytes[0] == 0x99);
}

void TestNullPeerNoOp() {
  ChatAetherWriteCompletionTestAccess::ApplyWriteStatus(
      nullptr, 1, 1, ae::WriteAction::Status::kSuccess);
}

}  // namespace

int main() {
  std::cerr << "chat_aether_write_completion_test start\n";
  TestTwoPeersSameTokenIncarnationSettleIndependently();
  TestStaleIncarnationIgnored();
  TestStaleTokenIgnored();
  TestRehashDoesNotChangeCallbackOwnership();
  TestDestroyedChannelStateIgnored();
  TestCaptureActiveWriteToPendingPreservesInFlight();
  TestNullPeerNoOp();
  std::cerr << "chat_aether_write_completion_test PASS\n";
  return 0;
}
