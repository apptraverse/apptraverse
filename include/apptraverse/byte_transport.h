#ifndef APPTRAVERSE_BYTE_TRANSPORT_H_
#define APPTRAVERSE_BYTE_TRANSPORT_H_

#include <cstdint>
#include <string>
#include <vector>

namespace apptraverse {

// Outgoing availability of one remote endpoint, as this transport instance
// observes it. Not a delivery receipt, not a shared snapshot field, and not
// restored from storage after restart.
//
// Online  — a send may be attempted. The peer may still drop the bytes.
// Offline — a send must not be attempted. Pending work waits.
// Unknown — this instance has no observation. Rate-limited attempts stay
//           allowed so the first connection is not blocked forever.
// Unknown is not Offline.
enum class EndpointAvailability : std::uint8_t {
  Unknown = 0,
  Online = 1,
  Offline = 2,
};

// Generic runtime transport: opaque bytes between endpoint identities. It
// knows nothing about SharedNode, Event, NodeState, ACK, or access rights.
// The receive binding is a runtime-only ctx + function pointer, owned by the
// binding runtime instance — no process-global or thread_local receiver.
//
// Availability is the same kind of binding: the adapter reports it, the
// caller does not keep a second copy. A→B and B→A are independent.
class IByteTransport {
 public:
  using ReceiveFn = void (*)(void* ctx, std::string const& source_endpoint,
                             std::vector<std::uint8_t> const& bytes);
  // Fired when this instance's observation of `endpoint` changes. Must not
  // send. The binding runtime records the change and services it later.
  using AvailabilityFn = void (*)(void* ctx, std::string const& endpoint,
                                  EndpointAvailability availability);

  virtual ~IByteTransport() = default;

  virtual std::string const& local_endpoint_uid() const = 0;
  virtual void Send(std::string const& destination_endpoint,
                    std::vector<std::uint8_t> bytes) = 0;
  virtual void BindReceive(void* ctx, ReceiveFn fn) = 0;
  virtual void ClearReceive() = 0;

  // Current observation of an outgoing endpoint. The default is Unknown:
  // an adapter that has no presence signal must not pretend to be Offline.
  virtual EndpointAvailability Availability(
      std::string const& endpoint) const {
    (void)endpoint;
    return EndpointAvailability::Unknown;
  }
  virtual void BindAvailability(void* ctx, AvailabilityFn fn) {
    (void)ctx;
    (void)fn;
  }
  virtual void ClearAvailability() {}
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_BYTE_TRANSPORT_H_
