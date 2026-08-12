#ifndef APPTRAVERSE_EXAMPLES_AETHER_P2P_OUTGOING_ROUTING_H_
#define APPTRAVERSE_EXAMPLES_AETHER_P2P_OUTGOING_ROUTING_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace apptraverse::examples {

enum class P2pStreamDirection : std::uint8_t {
  kIncoming,
  kOutgoing,
};

enum class P2pOutgoingState : std::uint8_t {
  kMissing,
  kConnecting,
  kWritable,
  kError,
};

enum class P2pSendDisposition : std::uint8_t {
  // Frame handed to an active outgoing that is not yet linked+writable.
  // NOT an end-to-end acknowledgement.
  kBufferedOrConnecting,
  // Frame handed to a linked+writable active outgoing. Still NOT an ACK.
  kWritable,
};

struct P2pSendResult {
  P2pSendDisposition disposition{P2pSendDisposition::kBufferedOrConnecting};
  std::uint64_t outgoing_generation{0};
};

// Pure routing metadata for one remote UID — no P2pStream dependency.
// Mirrors the runtime PeerTransport rules used by AetherP2pTransport.
struct OutgoingRoutingSession {
  P2pStreamDirection direction{P2pStreamDirection::kIncoming};
  std::uint64_t generation{0};
  bool active_for_send{false};
  bool is_writable{false};
  bool is_link_error{false};
};

struct OutgoingRoutingPeer {
  std::uint64_t active_outgoing_generation{0};
  P2pOutgoingState last_published_outgoing_state{P2pOutgoingState::kMissing};
  std::vector<OutgoingRoutingSession> sessions;
};

inline std::string_view ToString(P2pStreamDirection direction) {
  switch (direction) {
    case P2pStreamDirection::kIncoming:
      return "incoming";
    case P2pStreamDirection::kOutgoing:
      return "outgoing";
  }
  return "unknown";
}

inline std::string_view ToString(P2pOutgoingState state) {
  switch (state) {
    case P2pOutgoingState::kMissing:
      return "missing";
    case P2pOutgoingState::kConnecting:
      return "connecting";
    case P2pOutgoingState::kWritable:
      return "writable";
    case P2pOutgoingState::kError:
      return "error";
  }
  return "unknown";
}

inline std::string_view ToString(P2pSendDisposition disposition) {
  switch (disposition) {
    case P2pSendDisposition::kBufferedOrConnecting:
      return "buffered_connecting";
    case P2pSendDisposition::kWritable:
      return "writable";
  }
  return "unknown";
}

P2pOutgoingState ComputeOutgoingState(OutgoingRoutingSession const* active);

// Returns index of the unique active_for_send outgoing session, if any.
std::optional<std::size_t> FindActiveOutgoingIndex(
    OutgoingRoutingPeer const& peer);

// If no active outgoing exists, appends one (generation = previous+1 or 1).
// Returns true when a new outgoing session was created.
bool EnsureOutgoingRouting(OutgoingRoutingPeer& peer);

// Retires the current active outgoing and creates a new generation.
std::uint64_t ReplaceOutgoingRouting(OutgoingRoutingPeer& peer);

// Receive activity must never change active_for_send / generation.
void NoteReceiveOnSession(OutgoingRoutingPeer& peer, std::size_t session_index);

// Resolve which session index Send must use. Creates outgoing via
// EnsureOutgoingRouting when missing. Never returns an incoming or retired
// outgoing index.
std::optional<std::size_t> ResolveSendSessionIndex(OutgoingRoutingPeer& peer);

P2pSendResult BuildSendResult(OutgoingRoutingPeer const& peer,
                              std::size_t session_index);

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_AETHER_P2P_OUTGOING_ROUTING_H_
