#ifndef APPTRAVERSE_BYTE_TRANSPORT_H_
#define APPTRAVERSE_BYTE_TRANSPORT_H_

#include <cstdint>
#include <string>
#include <vector>

namespace apptraverse {

// Generic runtime transport: opaque bytes between endpoint identities. It
// knows nothing about SharedNode, Event, NodeState, ACK, or access rights.
// The receive binding is a runtime-only ctx + function pointer, owned by the
// binding runtime instance — no process-global or thread_local receiver.
class IByteTransport {
 public:
  using ReceiveFn = void (*)(void* ctx, std::string const& source_endpoint,
                             std::vector<std::uint8_t> const& bytes);

  virtual ~IByteTransport() = default;

  virtual std::string const& local_endpoint_uid() const = 0;
  virtual void Send(std::string const& destination_endpoint,
                    std::vector<std::uint8_t> bytes) = 0;
  virtual void BindReceive(void* ctx, ReceiveFn fn) = 0;
  virtual void ClearReceive() = 0;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_BYTE_TRANSPORT_H_
