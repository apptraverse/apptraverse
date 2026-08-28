#ifndef APPTRAVERSE_SHARED_TRANSPORT_H_
#define APPTRAVERSE_SHARED_TRANSPORT_H_

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "apptraverse/shared_event_id.h"
#include "apptraverse/shared_event_order.h"

namespace apptraverse {

enum class SharedFrameType : std::uint8_t {
  kEvent = 1,
  kAck = 2,
};

struct SharedEventFrame {
  std::string shared_room_id;
  SharedEventId event_id;
  SharedEventOrder order;
  std::vector<std::uint8_t> payload;
};

struct SharedAckFrame {
  std::string shared_room_id;
  SharedEventId event_id;
};

enum class SharedTransportEnqueueResult : std::uint8_t {
  Queued = 0,
  Failed = 1,
};

// Application-level transport for shared journal replication.
// Implementations must not call Aether APIs on the Model thread; they queue
// outbound bytes for the Aether thread (or a fake bridge in tests).
// Send* must not silently drop frames: return Failed or keep Queued until
// a peer stream exists.
class ISharedTransport {
 public:
  virtual ~ISharedTransport() = default;

  virtual SharedTransportEnqueueResult SendEvent(
      std::string const& peer_uid, SharedEventFrame const& frame) = 0;
  virtual SharedTransportEnqueueResult SendAck(
      std::string const& peer_uid, SharedAckFrame const& frame) = 0;
};

using SharedEventReceivedCallback =
    std::function<void(std::string const& from_peer_uid,
                       SharedEventFrame const& frame)>;
using SharedAckReceivedCallback =
    std::function<void(std::string const& from_peer_uid,
                       SharedAckFrame const& frame)>;

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_TRANSPORT_H_
