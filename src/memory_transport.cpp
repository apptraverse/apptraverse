#include "apptraverse/memory_transport.h"

#include <cassert>
#include <utility>

namespace apptraverse {
namespace {

std::vector<std::uint8_t> const kNoBytes{};

}  // namespace

std::size_t MemoryNetwork::PendingCount(std::string const& from,
                                        std::string const& to) const {
  auto const it = queues_.find(Direction{from, to});
  if (it == queues_.end()) {
    return 0;
  }
  return it->second.size();
}

std::vector<std::uint8_t> const& MemoryNetwork::PeekNext(
    std::string const& from, std::string const& to) const {
  auto const it = queues_.find(Direction{from, to});
  if (it == queues_.end() || it->second.empty()) {
    return kNoBytes;
  }
  return it->second.front();
}

bool MemoryNetwork::DeliverNext(std::string const& from,
                                std::string const& to) {
  if (!IsConnected(from, to)) {
    return false;
  }
  auto const queue = queues_.find(Direction{from, to});
  if (queue == queues_.end() || queue->second.empty()) {
    return false;
  }
  auto const endpoint = endpoints_.find(to);
  if (endpoint == endpoints_.end()) {
    // Destination replica is down: the packet waits for it to come back.
    return false;
  }
  auto bytes = std::move(queue->second.front());
  queue->second.pop_front();
  endpoint->second->Deliver(from, bytes);
  return true;
}

bool MemoryNetwork::DropNext(std::string const& from, std::string const& to) {
  auto const queue = queues_.find(Direction{from, to});
  if (queue == queues_.end() || queue->second.empty()) {
    return false;
  }
  queue->second.pop_front();
  return true;
}

bool MemoryNetwork::DuplicateNext(std::string const& from,
                                  std::string const& to) {
  auto const queue = queues_.find(Direction{from, to});
  if (queue == queues_.end() || queue->second.empty()) {
    return false;
  }
  queue->second.push_back(queue->second.front());
  return true;
}

bool MemoryNetwork::DeferNext(std::string const& from, std::string const& to) {
  auto const queue = queues_.find(Direction{from, to});
  if (queue == queues_.end() || queue->second.size() < 2) {
    return false;
  }
  auto head = std::move(queue->second.front());
  queue->second.pop_front();
  queue->second.push_back(std::move(head));
  return true;
}

bool MemoryNetwork::CorruptNext(std::string const& from,
                                std::string const& to) {
  auto const queue = queues_.find(Direction{from, to});
  if (queue == queues_.end() || queue->second.empty() ||
      queue->second.front().empty()) {
    return false;
  }
  auto& packet = queue->second.front();
  // Protocol version sits at byte 1. A bad version is not a frame.
  packet[packet.size() < 2 ? 0 : 1] ^= static_cast<std::uint8_t>(0xFF);
  return true;
}

void MemoryNetwork::ClearQueues() { queues_.clear(); }

void MemoryNetwork::Disconnect(std::string const& from,
                               std::string const& to) {
  disconnected_.insert(Direction{from, to});
}

void MemoryNetwork::Reconnect(std::string const& from, std::string const& to) {
  disconnected_.erase(Direction{from, to});
}

bool MemoryNetwork::IsConnected(std::string const& from,
                                std::string const& to) const {
  return disconnected_.find(Direction{from, to}) == disconnected_.end();
}

void MemoryNetwork::SetAvailability(std::string const& from,
                                    std::string const& to,
                                    EndpointAvailability availability) {
  auto const endpoint = endpoints_.find(from);
  if (endpoint == endpoints_.end()) {
    return;
  }
  endpoint->second->NoteAvailability(to, availability);
}

EndpointAvailability MemoryNetwork::Availability(std::string const& from,
                                                 std::string const& to) const {
  auto const endpoint = endpoints_.find(from);
  if (endpoint == endpoints_.end()) {
    return EndpointAvailability::Unknown;
  }
  return endpoint->second->Availability(to);
}

void MemoryNetwork::Attach(MemoryTransport& transport) {
  auto const [_, inserted] =
      endpoints_.emplace(transport.local_endpoint_uid(), &transport);
  assert(inserted && "endpoint uid is already attached to this network");
  (void)inserted;
}

void MemoryNetwork::Detach(MemoryTransport& transport) {
  endpoints_.erase(transport.local_endpoint_uid());
}

void MemoryNetwork::Enqueue(std::string const& from, std::string const& to,
                            std::vector<std::uint8_t> bytes) {
  if (!IsConnected(from, to)) {
    // Directional outage: the transport gave no delivery guarantee.
    return;
  }
  queues_[Direction{from, to}].push_back(std::move(bytes));
}

MemoryTransport::MemoryTransport(MemoryNetwork& network,
                                 std::string local_endpoint_uid)
    : network_{network}, local_endpoint_uid_{std::move(local_endpoint_uid)} {
  assert(!local_endpoint_uid_.empty());
  network_.Attach(*this);
}

MemoryTransport::~MemoryTransport() { network_.Detach(*this); }

void MemoryTransport::Send(std::string const& destination_endpoint,
                           std::vector<std::uint8_t> bytes) {
  assert(destination_endpoint != local_endpoint_uid_);
  network_.Enqueue(local_endpoint_uid_, destination_endpoint, std::move(bytes));
}

void MemoryTransport::BindReceive(void* ctx, ReceiveFn fn) {
  receive_ctx_ = ctx;
  receive_fn_ = fn;
}

void MemoryTransport::ClearReceive() {
  receive_ctx_ = nullptr;
  receive_fn_ = nullptr;
}

EndpointAvailability MemoryTransport::Availability(
    std::string const& endpoint) const {
  auto const it = availability_.find(endpoint);
  if (it == availability_.end()) {
    return EndpointAvailability::Unknown;
  }
  return it->second;
}

void MemoryTransport::NoteAvailability(std::string const& endpoint,
                                       EndpointAvailability availability) {
  auto const it = availability_.find(endpoint);
  if (it != availability_.end()) {
    if (it->second == availability) {
      return;
    }
    it->second = availability;
  } else if (availability == EndpointAvailability::Unknown) {
    return;
  } else {
    availability_.emplace(endpoint, availability);
  }
  if (availability_fn_ == nullptr) {
    return;
  }
  availability_fn_(availability_ctx_, endpoint, availability);
}

void MemoryTransport::BindAvailability(void* ctx, AvailabilityFn fn) {
  availability_ctx_ = ctx;
  availability_fn_ = fn;
}

void MemoryTransport::ClearAvailability() {
  availability_ctx_ = nullptr;
  availability_fn_ = nullptr;
}

void MemoryTransport::Deliver(std::string const& source_endpoint,
                              std::vector<std::uint8_t> const& bytes) {
  assert(receive_fn_ != nullptr &&
         "attached endpoint must have a bound receiver");
  receive_fn_(receive_ctx_, source_endpoint, bytes);
}

}  // namespace apptraverse
