#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace apptraverse {
enum class LinkAvailability { Unknown, Online, Offline };

// Runtime transport boundary. There are no model pointers or serialization
// rules here: callers send the bytes produced by the object system.
class IByteLink {
 public:
  using Receiver = std::function<void(std::string const& source,
                                      std::vector<std::uint8_t> const& bytes)>;

  virtual ~IByteLink() = default;
  virtual void SetReceiver(Receiver receiver) = 0;
  // true accepts the complete frame for transmission, not delivery/ACK.
  // false accepts nothing (e.g. local backpressure); admission may be retried
  // on the next service turn without consuming the remote ACK timeout.
  virtual bool Send(std::string const& destination,
                    std::vector<std::uint8_t> const& bytes) = 0;
  // Optional infrastructure hint for immutable native packets. Ordinary links
  // preserve Send semantics. A mailbox uses this identity for upstream retries.
  virtual bool SendIdentified(std::string const& destination,
                              std::string const& /*identity*/,
                              std::vector<std::uint8_t> const& bytes) {
    return Send(destination, bytes);
  }
  virtual void ForgetIdentified(std::string const&, std::string const&) {}
  // Control replies are reproducible from a retried durable packet and do not
  // need an auxiliary offline copy (avoids ACK-on-ACK storage obligations).
  virtual bool SendTransient(std::string const& destination,
                             std::vector<std::uint8_t> const& bytes) {
    return Send(destination, bytes);
  }
  virtual LinkAvailability Availability(
      std::string const& destination) const = 0;
};

}  // namespace apptraverse
