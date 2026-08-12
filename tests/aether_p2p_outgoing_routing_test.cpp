#include <iostream>
#include <string>

#include "aether_p2p_outgoing_routing.h"

using apptraverse::examples::BuildSendResult;
using apptraverse::examples::EnsureOutgoingRouting;
using apptraverse::examples::FindActiveOutgoingIndex;
using apptraverse::examples::NoteReceiveOnSession;
using apptraverse::examples::OutgoingRoutingPeer;
using apptraverse::examples::OutgoingRoutingSession;
using apptraverse::examples::P2pSendDisposition;
using apptraverse::examples::P2pStreamDirection;
using apptraverse::examples::ReplaceOutgoingRouting;
using apptraverse::examples::ResolveSendSessionIndex;
using apptraverse::examples::ToString;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::cerr << "CHECK failed: " #cond " at " << __FILE__ << ":"      \
                << __LINE__ << "\n";                                      \
      return 1;                                                           \
    }                                                                     \
  } while (0)

namespace {

OutgoingRoutingSession MakeIncoming(bool writable) {
  OutgoingRoutingSession session;
  session.direction = P2pStreamDirection::kIncoming;
  session.active_for_send = false;
  session.is_writable = writable;
  return session;
}

OutgoingRoutingSession MakeOutgoing(std::uint64_t generation, bool active,
                                    bool writable) {
  OutgoingRoutingSession session;
  session.direction = P2pStreamDirection::kOutgoing;
  session.generation = generation;
  session.active_for_send = active;
  session.is_writable = writable;
  return session;
}

}  // namespace

int main() {
  // A. Only incoming: not used for Send; EnsureOutgoing creates outgoing.
  {
    OutgoingRoutingPeer peer;
    peer.sessions.push_back(MakeIncoming(true));
    CHECK(!FindActiveOutgoingIndex(peer).has_value());
    CHECK(EnsureOutgoingRouting(peer));
    auto const send_idx = ResolveSendSessionIndex(peer);
    CHECK(send_idx.has_value());
    CHECK(peer.sessions[*send_idx].direction == P2pStreamDirection::kOutgoing);
    CHECK(peer.sessions[*send_idx].active_for_send);
    CHECK(peer.sessions[*send_idx].generation == 1);
    CHECK(peer.sessions[0].direction == P2pStreamDirection::kIncoming);
    CHECK(!peer.sessions[0].active_for_send);
  }

  // B. Incoming writable + outgoing connecting → Send uses outgoing.
  {
    OutgoingRoutingPeer peer;
    peer.sessions.push_back(MakeIncoming(true));
    peer.sessions.push_back(MakeOutgoing(1, true, false));
    peer.active_outgoing_generation = 1;
    auto const send_idx = ResolveSendSessionIndex(peer);
    CHECK(send_idx.has_value());
    CHECK(*send_idx == 1);
    CHECK(peer.sessions[*send_idx].generation == 1);
    auto const result = BuildSendResult(peer, *send_idx);
    CHECK(result.disposition == P2pSendDisposition::kBufferedOrConnecting);
    CHECK(result.outgoing_generation == 1);
  }

  // C. Retired writable outgoing + new connecting active → Send uses new.
  {
    OutgoingRoutingPeer peer;
    peer.sessions.push_back(MakeOutgoing(1, false, true));  // retired writable
    peer.sessions.push_back(MakeOutgoing(2, true, false));  // active connecting
    peer.active_outgoing_generation = 2;
    auto const send_idx = ResolveSendSessionIndex(peer);
    CHECK(send_idx.has_value());
    CHECK(*send_idx == 1);
    CHECK(peer.sessions[*send_idx].generation == 2);
    CHECK(!peer.sessions[0].active_for_send);
  }

  // D. Receive on incoming does not change active outgoing.
  {
    OutgoingRoutingPeer peer;
    peer.sessions.push_back(MakeIncoming(true));
    peer.sessions.push_back(MakeOutgoing(1, true, false));
    peer.active_outgoing_generation = 1;
    auto const before = FindActiveOutgoingIndex(peer);
    NoteReceiveOnSession(peer, 0);
    auto const after = FindActiveOutgoingIndex(peer);
    CHECK(before == after);
    CHECK(peer.active_outgoing_generation == 1);
    CHECK(peer.sessions[1].active_for_send);
  }

  // E. Receive on retired outgoing does not change active outgoing.
  {
    OutgoingRoutingPeer peer;
    peer.sessions.push_back(MakeOutgoing(1, false, true));
    peer.sessions.push_back(MakeOutgoing(2, true, false));
    peer.active_outgoing_generation = 2;
    NoteReceiveOnSession(peer, 0);
    auto const active = FindActiveOutgoingIndex(peer);
    CHECK(active.has_value());
    CHECK(*active == 1);
    CHECK(peer.sessions[1].generation == 2);
  }

  // F. ReplaceOutgoing bumps generation; old not send candidate; new gets Writes.
  {
    OutgoingRoutingPeer peer;
    peer.sessions.push_back(MakeOutgoing(1, true, true));
    peer.active_outgoing_generation = 1;
    auto const new_gen = ReplaceOutgoingRouting(peer);
    CHECK(new_gen == 2);
    CHECK(peer.active_outgoing_generation == 2);
    CHECK(!peer.sessions[0].active_for_send);
    CHECK(peer.sessions[0].generation == 1);
    auto const send_idx = ResolveSendSessionIndex(peer);
    CHECK(send_idx.has_value());
    CHECK(*send_idx == 1);
    CHECK(peer.sessions[*send_idx].generation == 2);
    CHECK(peer.sessions[*send_idx].active_for_send);
  }

  // G. Connect / EnsureOutgoing with existing incoming still creates outgoing.
  {
    OutgoingRoutingPeer peer;
    peer.sessions.push_back(MakeIncoming(true));
    CHECK(EnsureOutgoingRouting(peer));
    CHECK(FindActiveOutgoingIndex(peer).has_value());
    CHECK(!EnsureOutgoingRouting(peer));  // already present
  }

  CHECK(ToString(P2pStreamDirection::kIncoming) == "incoming");
  CHECK(ToString(P2pStreamDirection::kOutgoing) == "outgoing");
  CHECK(ToString(P2pSendDisposition::kWritable) == "writable");
  CHECK(ToString(P2pSendDisposition::kBufferedOrConnecting) ==
        "buffered_connecting");

  std::cout << "aether_p2p_outgoing_routing_test OK\n";
  return 0;
}
