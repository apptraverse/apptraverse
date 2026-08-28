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

// Application-level transport for shared journal replication.
class ISharedTransport {
 public:
  virtual ~ISharedTransport() = default;

  virtual void SendEvent(SharedEventFrame const& frame) = 0;
  virtual void SendAck(SharedAckFrame const& frame) = 0;
};

using SharedEventReceivedCallback =
    std::function<void(std::string const& from_peer_uid,
                       SharedEventFrame const& frame)>;
using SharedAckReceivedCallback =
    std::function<void(std::string const& from_peer_uid,
                       SharedAckFrame const& frame)>;

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_TRANSPORT_H_
