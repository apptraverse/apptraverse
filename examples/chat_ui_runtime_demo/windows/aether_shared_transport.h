#ifndef APPTRAVERSE_AETHER_SHARED_TRANSPORT_H_
#define APPTRAVERSE_AETHER_SHARED_TRANSPORT_H_

#include <string>
#include <vector>

#include "apptraverse/shared_frame_codec.h"
#include "apptraverse/shared_transport.h"

#include "aether_runtime.h"

namespace apptraverse {

// Model-thread transport: encode frames and queue them onto ChatAetherRuntime.
// Never touches Aether objects directly.
class AetherSharedTransport final : public ISharedTransport {
 public:
  explicit AetherSharedTransport(ChatAetherRuntime& runtime)
      : runtime_{runtime} {}

  void SendEvent(std::string const& peer_uid,
                 SharedEventFrame const& frame) override {
    runtime_.SendPeerFrame(peer_uid, EncodeSharedEventFrame(frame));
  }

  void SendAck(std::string const& peer_uid,
               SharedAckFrame const& frame) override {
    runtime_.SendPeerFrame(peer_uid, EncodeSharedAckFrame(frame));
  }

 private:
  ChatAetherRuntime& runtime_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_AETHER_SHARED_TRANSPORT_H_
