#include <cstdlib>
#include <iostream>
#include <vector>

#include "aether_p2p_stream_select.h"

namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

using apptraverse::examples::AggregatePeerTransportState;
using apptraverse::examples::P2pPeerTransportState;
using apptraverse::examples::P2pSessionSelectCandidate;
using apptraverse::examples::P2pStreamSelectReason;
using apptraverse::examples::SelectBestP2pSession;
using apptraverse::examples::ToString;

P2pSessionSelectCandidate Candidate(std::uint64_t creation_order,
                                    std::uint64_t last_receive_order,
                                    bool is_writable, bool is_link_error) {
  P2pSessionSelectCandidate candidate;
  candidate.creation_order = creation_order;
  candidate.last_receive_order = last_receive_order;
  candidate.is_writable = is_writable;
  candidate.is_link_error = is_link_error;
  return candidate;
}

void TestEmpty() {
  std::vector<P2pSessionSelectCandidate> const candidates;
  CHECK(!SelectBestP2pSession(candidates).has_value());
  CHECK(AggregatePeerTransportState(candidates) ==
        P2pPeerTransportState::kNoStream);
}

// A. The old outgoing stream is still connecting while a fresh incoming stream
// already carries traffic — the incoming one must win.
void TestWritableIncomingBeatsConnectingOutgoing() {
  std::vector<P2pSessionSelectCandidate> const candidates{
      Candidate(1, 0, false, false),
      Candidate(2, 0, true, false),
  };
  auto const best = SelectBestP2pSession(candidates);
  CHECK(best.has_value());
  CHECK(best->index == 1);
  CHECK(best->reason == P2pStreamSelectReason::kWritableNewest);
  CHECK(AggregatePeerTransportState(candidates) ==
        P2pPeerTransportState::kWritable);
}

// B. Two writable streams — recent receive wins when it is on the newest
// stream (or tied). A newer silent writable beats a stale older receive so a
// dead pre-reconnect stream cannot keep winning.
void TestRecentReceiveWins() {
  std::vector<P2pSessionSelectCandidate> const candidates{
      Candidate(1, 7, true, false),
      Candidate(2, 9, true, false),
  };
  auto const best = SelectBestP2pSession(candidates);
  CHECK(best.has_value());
  CHECK(best->index == 1);
  CHECK(best->reason == P2pStreamSelectReason::kRecentReceive);

  std::vector<P2pSessionSelectCandidate> const newer_also_received{
      Candidate(1, 9, true, false),
      Candidate(2, 7, true, false),
  };
  auto const newer_best = SelectBestP2pSession(newer_also_received);
  CHECK(newer_best.has_value());
  // Newer stream exists, so stale higher receive on the older one loses.
  CHECK(newer_best->index == 1);
  CHECK(newer_best->reason == P2pStreamSelectReason::kWritableNewest);
}

// After reconnect a fresh incoming writable must beat an older stream that
// still looks "recent" from traffic before the outage.
void TestNewerWritableBeatsStaleReceive() {
  std::vector<P2pSessionSelectCandidate> const candidates{
      Candidate(1, 40, true, false),
      Candidate(5, 0, true, false),
  };
  auto const best = SelectBestP2pSession(candidates);
  CHECK(best.has_value());
  CHECK(best->index == 1);
  CHECK(best->reason == P2pStreamSelectReason::kWritableNewest);
}

// C. Two writable streams, neither ever received — newest wins.
void TestWritableNewestWins() {
  std::vector<P2pSessionSelectCandidate> const candidates{
      Candidate(4, 0, true, false),
      Candidate(9, 0, true, false),
      Candidate(6, 0, true, false),
  };
  auto const best = SelectBestP2pSession(candidates);
  CHECK(best.has_value());
  CHECK(best->index == 1);
  CHECK(best->reason == P2pStreamSelectReason::kWritableNewest);
}

// D. Nothing writable — the connecting stream beats the errored one regardless
// of creation order.
void TestConnectingBeatsError() {
  std::vector<P2pSessionSelectCandidate> const candidates{
      Candidate(1, 0, false, false),
      Candidate(2, 0, false, true),
  };
  auto const best = SelectBestP2pSession(candidates);
  CHECK(best.has_value());
  CHECK(best->index == 0);
  CHECK(best->reason == P2pStreamSelectReason::kConnectingNewest);
  CHECK(AggregatePeerTransportState(candidates) ==
        P2pPeerTransportState::kConnecting);

  std::vector<P2pSessionSelectCandidate> const two_connecting{
      Candidate(1, 0, false, false),
      Candidate(3, 0, false, false),
      Candidate(2, 0, false, true),
  };
  auto const newest = SelectBestP2pSession(two_connecting);
  CHECK(newest.has_value());
  CHECK(newest->index == 1);
  CHECK(newest->reason == P2pStreamSelectReason::kConnectingNewest);
}

// E. Everything errored — fall back to the newest stream so a Restream has
// something to act on.
void TestErrorFallback() {
  std::vector<P2pSessionSelectCandidate> const candidates{
      Candidate(3, 0, false, true),
      Candidate(8, 0, false, true),
      Candidate(5, 0, false, true),
  };
  auto const best = SelectBestP2pSession(candidates);
  CHECK(best.has_value());
  CHECK(best->index == 1);
  CHECK(best->reason == P2pStreamSelectReason::kErrorFallback);
  CHECK(AggregatePeerTransportState(candidates) ==
        P2pPeerTransportState::kError);
}

// F. Ranking never reaches beyond the span it was given: the caller filters by
// peer UID, so a writable stream belonging to another peer cannot be picked.
void TestSelectionStaysInsideProvidedSpan() {
  std::vector<P2pSessionSelectCandidate> const all_sessions{
      Candidate(1, 0, false, false),   // peer A, connecting
      Candidate(2, 11, true, false),   // peer B, writable and receiving
      Candidate(3, 0, false, true),    // peer A, errored
  };

  std::vector<P2pSessionSelectCandidate> const peer_a{all_sessions[0],
                                                      all_sessions[2]};
  auto const best_a = SelectBestP2pSession(peer_a);
  CHECK(best_a.has_value());
  CHECK(best_a->index == 0);
  CHECK(best_a->reason == P2pStreamSelectReason::kConnectingNewest);
  CHECK(AggregatePeerTransportState(peer_a) ==
        P2pPeerTransportState::kConnecting);

  std::vector<P2pSessionSelectCandidate> const peer_b{all_sessions[1]};
  auto const best_b = SelectBestP2pSession(peer_b);
  CHECK(best_b.has_value());
  CHECK(best_b->index == 0);
  CHECK(best_b->reason == P2pStreamSelectReason::kRecentReceive);
  CHECK(AggregatePeerTransportState(peer_b) ==
        P2pPeerTransportState::kWritable);
}

void TestMarkerNames() {
  using apptraverse::examples::P2pStreamDirection;
  CHECK(ToString(P2pStreamDirection::kIncoming) == "incoming");
  CHECK(ToString(P2pStreamDirection::kOutgoing) == "outgoing");

  CHECK(ToString(P2pPeerTransportState::kNoStream) == "no_stream");
  CHECK(ToString(P2pPeerTransportState::kConnecting) == "connecting");
  CHECK(ToString(P2pPeerTransportState::kWritable) == "writable");
  CHECK(ToString(P2pPeerTransportState::kError) == "error");

  CHECK(ToString(P2pStreamSelectReason::kRecentReceive) == "recent_receive");
  CHECK(ToString(P2pStreamSelectReason::kWritableNewest) == "writable_newest");
  CHECK(ToString(P2pStreamSelectReason::kConnectingNewest) ==
        "connecting_newest");
  CHECK(ToString(P2pStreamSelectReason::kErrorFallback) == "error_fallback");
}

}  // namespace

int main() {
  TestEmpty();
  TestWritableIncomingBeatsConnectingOutgoing();
  TestRecentReceiveWins();
  TestNewerWritableBeatsStaleReceive();
  TestWritableNewestWins();
  TestConnectingBeatsError();
  TestErrorFallback();
  TestSelectionStaysInsideProvidedSpan();
  TestMarkerNames();
  std::cout << "aether_p2p_stream_select_test OK\n";
  return 0;
}
