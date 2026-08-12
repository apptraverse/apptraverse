#include "aether_p2p_stream_select.h"

namespace apptraverse::examples {

std::optional<P2pSessionSelectResult> SelectBestP2pSession(
    std::span<P2pSessionSelectCandidate const> candidates) {
  if (candidates.empty()) {
    return std::nullopt;
  }

  std::optional<std::size_t> best_writable_recv;
  std::optional<std::size_t> best_writable_new;
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    auto const& c = candidates[i];
    if (!c.is_writable) {
      continue;
    }
    if (c.last_receive_order > 0) {
      if (!best_writable_recv.has_value() ||
          c.last_receive_order >
              candidates[*best_writable_recv].last_receive_order) {
        best_writable_recv = i;
      }
    }
    if (!best_writable_new.has_value() ||
        c.creation_order > candidates[*best_writable_new].creation_order) {
      best_writable_new = i;
    }
  }

  if (best_writable_recv.has_value()) {
    return P2pSessionSelectResult{*best_writable_recv,
                                  P2pStreamSelectReason::kRecentReceive};
  }
  if (best_writable_new.has_value()) {
    return P2pSessionSelectResult{*best_writable_new,
                                  P2pStreamSelectReason::kWritableNewest};
  }

  std::optional<std::size_t> best_connecting;
  std::size_t best_any = 0;
  for (std::size_t i = 0; i < candidates.size(); ++i) {
    auto const& c = candidates[i];
    if (c.creation_order > candidates[best_any].creation_order) {
      best_any = i;
    }
    if (c.is_link_error) {
      continue;
    }
    if (!best_connecting.has_value() ||
        c.creation_order > candidates[*best_connecting].creation_order) {
      best_connecting = i;
    }
  }

  if (best_connecting.has_value()) {
    return P2pSessionSelectResult{*best_connecting,
                                  P2pStreamSelectReason::kConnectingNewest};
  }
  return P2pSessionSelectResult{best_any,
                                P2pStreamSelectReason::kErrorFallback};
}

P2pPeerTransportState AggregatePeerTransportState(
    std::span<P2pSessionSelectCandidate const> candidates) {
  if (candidates.empty()) {
    return P2pPeerTransportState::kNoStream;
  }
  bool any_non_error = false;
  for (auto const& c : candidates) {
    if (c.is_writable) {
      return P2pPeerTransportState::kWritable;
    }
    if (!c.is_link_error) {
      any_non_error = true;
    }
  }
  return any_non_error ? P2pPeerTransportState::kConnecting
                       : P2pPeerTransportState::kError;
}

}  // namespace apptraverse::examples
