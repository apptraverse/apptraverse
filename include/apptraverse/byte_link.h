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
  virtual LinkAvailability Availability(
      std::string const& destination) const = 0;
};

}  // namespace apptraverse
