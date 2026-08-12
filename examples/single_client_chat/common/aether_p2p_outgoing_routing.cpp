#include "aether_p2p_outgoing_routing.h"

namespace apptraverse::examples {

P2pOutgoingState ComputeOutgoingState(OutgoingRoutingSession const* active) {
  if (active == nullptr) {
    return P2pOutgoingState::kMissing;
  }
  if (active->is_writable) {
    return P2pOutgoingState::kWritable;
  }
  if (active->is_link_error) {
    return P2pOutgoingState::kError;
  }
  return P2pOutgoingState::kConnecting;
}

std::optional<std::size_t> FindActiveOutgoingIndex(
    OutgoingRoutingPeer const& peer) {
  for (std::size_t i = 0; i < peer.sessions.size(); ++i) {
    auto const& session = peer.sessions[i];
    if (session.direction == P2pStreamDirection::kOutgoing &&
        session.active_for_send) {
      return i;
    }
  }
  return std::nullopt;
}

bool EnsureOutgoingRouting(OutgoingRoutingPeer& peer) {
  if (FindActiveOutgoingIndex(peer).has_value()) {
    return false;
  }
  OutgoingRoutingSession session;
  session.direction = P2pStreamDirection::kOutgoing;
  session.generation = peer.active_outgoing_generation + 1;
  if (session.generation == 0) {
    session.generation = 1;
  }
  session.active_for_send = true;
  peer.active_outgoing_generation = session.generation;
  peer.sessions.push_back(session);
  return true;
}

std::uint64_t ReplaceOutgoingRouting(OutgoingRoutingPeer& peer) {
  auto const active = FindActiveOutgoingIndex(peer);
  if (active.has_value()) {
    peer.sessions[*active].active_for_send = false;
  }
  OutgoingRoutingSession session;
  session.direction = P2pStreamDirection::kOutgoing;
  session.generation = peer.active_outgoing_generation + 1;
  if (session.generation == 0) {
    session.generation = 1;
  }
  session.active_for_send = true;
  peer.active_outgoing_generation = session.generation;
  peer.sessions.push_back(session);
  return session.generation;
}

void NoteReceiveOnSession(OutgoingRoutingPeer& peer, std::size_t session_index) {
  (void)peer;
  (void)session_index;
  // Intentionally empty: receive must not alter outgoing selection.
}

std::optional<std::size_t> ResolveSendSessionIndex(OutgoingRoutingPeer& peer) {
  (void)EnsureOutgoingRouting(peer);
  return FindActiveOutgoingIndex(peer);
}

P2pSendResult BuildSendResult(OutgoingRoutingPeer const& peer,
                              std::size_t session_index) {
  P2pSendResult result;
  if (session_index >= peer.sessions.size()) {
    return result;
  }
  auto const& session = peer.sessions[session_index];
  result.outgoing_generation = session.generation;
  result.disposition = session.is_writable
                           ? P2pSendDisposition::kWritable
                           : P2pSendDisposition::kBufferedOrConnecting;
  return result;
}

}  // namespace apptraverse::examples
