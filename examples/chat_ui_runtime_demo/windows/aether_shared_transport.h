#ifndef CHAT_AETHER_SHARED_TRANSPORT_H_
#define CHAT_AETHER_SHARED_TRANSPORT_H_

#include <string>
#include <vector>

#include "apptraverse/shared_frame_codec.h"
#include "apptraverse/shared_transport.h"

#include "aether_runtime.h"

namespace chat {

using apptraverse::ISharedTransport;
using apptraverse::SharedAckFrame;
using apptraverse::SharedEventFrame;
using apptraverse::SharedTransportEnqueueResult;

class AetherSharedTransport final : public ISharedTransport {
 public:
  explicit AetherSharedTransport(ChatAetherRuntime& runtime)
      : runtime_{runtime} {}

  SharedTransportEnqueueResult SendEvent(std::string const& peer_uid,
                                         SharedEventFrame const& frame) override {
    runtime_.SendPeerFrame(peer_uid, EncodeSharedEventFrame(frame));
    return SharedTransportEnqueueResult::Queued;
  }

  SharedTransportEnqueueResult SendAck(std::string const& peer_uid,
                                       SharedAckFrame const& frame) override {
    runtime_.SendPeerFrame(peer_uid, EncodeSharedAckFrame(frame));
    return SharedTransportEnqueueResult::Queued;
  }

 private:
  ChatAetherRuntime& runtime_;
};

}  // namespace chat

#endif  // CHAT_AETHER_SHARED_TRANSPORT_H_
