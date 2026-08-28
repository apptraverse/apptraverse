#include "apptraverse/shared_runtime.h"

namespace apptraverse {

SharedRuntime::SharedRuntime(SharedRuntimeConfig config)
    : config_{std::move(config)} {}

void SharedRuntime::SetPeerOnline(PeerDeliveryState& peer, bool online) {
  peer.online = online;
}

}  // namespace apptraverse
