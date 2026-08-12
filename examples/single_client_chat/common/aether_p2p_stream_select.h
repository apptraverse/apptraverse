#ifndef APPTRAVERSE_EXAMPLES_AETHER_P2P_STREAM_SELECT_H_
#define APPTRAVERSE_EXAMPLES_AETHER_P2P_STREAM_SELECT_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace apptraverse::examples {

enum class P2pStreamDirection : std::uint8_t {
  kIncoming,
  kOutgoing,
};

enum class P2pPeerTransportState : std::uint8_t {
  kNoStream,
  kConnecting,
  kWritable,
  kError,
};

enum class P2pSendDisposition : std::uint8_t {
  // Packet was given to a stream that is not yet linked+writable.
  // The stream may buffer locally; this is NOT an ACK.
  kBufferedOrConnecting,
  // Packet was given to a linked+writable stream. Still NOT an ACK.
  kWritable,
};

enum class P2pStreamSelectReason : std::uint8_t {
  kRecentReceive,
  kWritableNewest,
  kConnectingNewest,
  kErrorFallback,
};

// Pure ranking input — no P2pStream dependency.
// is_writable means link_state==kLinked && stream_info().is_writable.
// is_link_error means link_state==kLinkError.
struct P2pSessionSelectCandidate {
  std::uint64_t creation_order{0};
  std::uint64_t last_receive_order{0};  // 0 = never received
  bool is_writable{false};
  bool is_link_error{false};
};

struct P2pSessionSelectResult {
  std::size_t index{0};
  P2pStreamSelectReason reason{P2pStreamSelectReason::kErrorFallback};
};

// Rank sessions that already belong to one peer UID.
// Empty span → nullopt. Never ranks by pointer address.
std::optional<P2pSessionSelectResult> SelectBestP2pSession(
    std::span<P2pSessionSelectCandidate const> candidates);

P2pPeerTransportState AggregatePeerTransportState(
    std::span<P2pSessionSelectCandidate const> candidates);

inline std::string_view ToString(P2pStreamDirection direction) {
  switch (direction) {
    case P2pStreamDirection::kIncoming:
      return "incoming";
    case P2pStreamDirection::kOutgoing:
      return "outgoing";
  }
  return "unknown";
}

inline std::string_view ToString(P2pPeerTransportState state) {
  switch (state) {
    case P2pPeerTransportState::kNoStream:
      return "no_stream";
    case P2pPeerTransportState::kConnecting:
      return "connecting";
    case P2pPeerTransportState::kWritable:
      return "writable";
    case P2pPeerTransportState::kError:
      return "error";
  }
  return "unknown";
}

inline std::string_view ToString(P2pStreamSelectReason reason) {
  switch (reason) {
    case P2pStreamSelectReason::kRecentReceive:
      return "recent_receive";
    case P2pStreamSelectReason::kWritableNewest:
      return "writable_newest";
    case P2pStreamSelectReason::kConnectingNewest:
      return "connecting_newest";
    case P2pStreamSelectReason::kErrorFallback:
      return "error_fallback";
  }
  return "unknown";
}

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_AETHER_P2P_STREAM_SELECT_H_
