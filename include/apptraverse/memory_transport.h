#ifndef APPTRAVERSE_MEMORY_TRANSPORT_H_
#define APPTRAVERSE_MEMORY_TRANSPORT_H_

#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "apptraverse/byte_transport.h"

namespace apptraverse {

class MemoryTransport;

// Deterministic in-process network. Packets queue per directed endpoint pair
// and move only when the caller asks: no threads, no timers, no sleeps.
// Queues outlive the endpoints, so a packet stays in flight across a replica
// restart.
class MemoryNetwork {
  friend class MemoryTransport;

 public:
  using Direction = std::pair<std::string, std::string>;

  std::size_t PendingCount(std::string const& from,
                           std::string const& to) const;
  std::vector<std::uint8_t> const& PeekNext(std::string const& from,
                                            std::string const& to) const;

  // Hand the queued head to the destination endpoint. False when the
  // direction is down, the queue is empty, or nothing is attached as
  // destination (the packet then stays queued).
  bool DeliverNext(std::string const& from, std::string const& to);
  bool DropNext(std::string const& from, std::string const& to);
  // Queue a second copy of the head: the same packet arrives twice.
  bool DuplicateNext(std::string const& from, std::string const& to);
  // Move the head behind every other queued packet on this direction.
  // False when fewer than two packets are queued — order cannot change.
  bool DeferNext(std::string const& from, std::string const& to);
  // Damage the head so the existing frame header check rejects it.
  // Flipping a payload byte is not enough: that byte can still be a legal value.
  bool CorruptNext(std::string const& from, std::string const& to);
  // Drop every queued packet. A restart test uses this so recovery cannot
  // depend on bytes the test network still held.
  void ClearQueues();

  // Directional outage. Sends are lost while it lasts, queued packets wait.
  void Disconnect(std::string const& from, std::string const& to);
  void Reconnect(std::string const& from, std::string const& to);
  bool IsConnected(std::string const& from, std::string const& to) const;

  // Reported outgoing availability. Independent of Disconnect: a direction
  // can look Online while Enqueue still drops, or Offline while a queue
  // still holds bytes. Missing entries are Unknown. Not serialized.
  // SetAvailability and Deliver run on the caller's context and invoke the
  // binding before returning. Notifies the source endpoint only when the
  // value changes.
  void SetAvailability(std::string const& from, std::string const& to,
                       EndpointAvailability availability);
  EndpointAvailability Availability(std::string const& from,
                                    std::string const& to) const;

 private:
  void Attach(MemoryTransport& transport);
  void Detach(MemoryTransport& transport);
  void Enqueue(std::string const& from, std::string const& to,
               std::vector<std::uint8_t> bytes);

  std::map<std::string, MemoryTransport*> endpoints_;
  std::map<Direction, std::deque<std::vector<std::uint8_t>>> queues_;
  std::set<Direction> disconnected_;
  std::map<Direction, EndpointAvailability> availability_;
};

// One replica's endpoint on a MemoryNetwork. It is attached for as long as it
// lives, so a replica restart is destroy + construct with the same endpoint
// uid. Endpoint identity is the MemoryLink descriptor uid; nothing here is
// persisted and no Link is marked local.
class MemoryTransport final : public IByteTransport {
 public:
  MemoryTransport(MemoryNetwork& network, std::string local_endpoint_uid);
  ~MemoryTransport() override;

  MemoryTransport(MemoryTransport const&) = delete;
  MemoryTransport& operator=(MemoryTransport const&) = delete;

  std::string const& local_endpoint_uid() const override {
    return local_endpoint_uid_;
  }

  void Send(std::string const& destination_endpoint,
            std::vector<std::uint8_t> bytes) override;
  void BindReceive(void* ctx, ReceiveFn fn) override;
  void ClearReceive() override;
  EndpointAvailability Availability(std::string const& endpoint) const override;
  void BindAvailability(void* ctx, AvailabilityFn fn) override;
  void ClearAvailability() override;

 private:
  friend class MemoryNetwork;

  void Deliver(std::string const& source_endpoint,
               std::vector<std::uint8_t> const& bytes);

  MemoryNetwork& network_;
  std::string local_endpoint_uid_;
  void* receive_ctx_{nullptr};
  ReceiveFn receive_fn_{nullptr};
  void* availability_ctx_{nullptr};
  AvailabilityFn availability_fn_{nullptr};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_MEMORY_TRANSPORT_H_
