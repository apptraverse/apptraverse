#ifndef APPTRAVERSE_MEMORY_LINK_H_
#define APPTRAVERSE_MEMORY_LINK_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace apptraverse {

enum class LinkAvailability { Unknown, Online, Offline };

// Runtime transport boundary. There are no model pointers or serialization
// rules here: callers send the bytes produced by the object system.
class IByteLink {
 public:
  using Receiver = std::function<void(
      std::string const& source, std::vector<std::uint8_t> const& bytes)>;

  virtual ~IByteLink() = default;
  virtual void SetReceiver(Receiver receiver) = 0;
  virtual bool Send(std::string const& destination,
                    std::vector<std::uint8_t> const& bytes) = 0;
  virtual LinkAvailability Availability(
      std::string const& destination) const = 0;
};

class MemoryLink;

// Deterministic test driver. AdvanceTo changes only the logical clock;
// Deliver chooses a due packet explicitly, allowing arbitrary wire ordering.
// Presence is an externally supplied observation in this first memory stand.
class MemoryLinkNetwork {
 public:
  using PacketId = std::uint64_t;
  struct Packet {
    PacketId id;
    std::string source;
    std::string destination;
    std::vector<std::uint8_t> bytes;
    std::uint64_t due_us;
  };

  MemoryLinkNetwork();
  ~MemoryLinkNetwork();
  MemoryLinkNetwork(MemoryLinkNetwork const&) = delete;
  MemoryLinkNetwork& operator=(MemoryLinkNetwork const&) = delete;

  std::uint64_t NowUs() const;
  void AdvanceTo(std::uint64_t now_us);
  std::vector<Packet> PendingPackets() const;
  bool Deliver(PacketId id);
  bool Drop(PacketId id);
  // Zero means the packet no longer exists.
  PacketId Duplicate(PacketId id);
  bool Delay(PacketId id, std::uint64_t delay_us);
  void SetAvailability(std::string const& endpoint,
                       LinkAvailability availability);

 private:
  struct State;
  std::shared_ptr<State> state_;
  friend class MemoryLink;
};

// An endpoint registration, with a runtime-only callback. Destroying a link
// unbinds it; queued packets retain identity/bytes and can reach a replacement
// link with the same endpoint after restart. A driver may Drop them instead.
class MemoryLink final : public IByteLink {
 public:
  MemoryLink(MemoryLinkNetwork& network, std::string endpoint);
  ~MemoryLink() override;
  MemoryLink(MemoryLink const&) = delete;
  MemoryLink& operator=(MemoryLink const&) = delete;

  std::string const& EndpointId() const;
  void SetReceiver(Receiver receiver) override;
  bool Send(std::string const& destination,
            std::vector<std::uint8_t> const& bytes) override;
  LinkAvailability Availability(std::string const& destination) const override;

 private:
  std::shared_ptr<MemoryLinkNetwork::State> state_;
  std::string endpoint_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_MEMORY_LINK_H_
