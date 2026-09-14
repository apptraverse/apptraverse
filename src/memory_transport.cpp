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

void MemoryTransport::Deliver(std::string const& source_endpoint,
                              std::vector<std::uint8_t> const& bytes) {
  assert(receive_fn_ != nullptr &&
         "attached endpoint must have a bound receiver");
  receive_fn_(receive_ctx_, source_endpoint, bytes);
}

}  // namespace apptraverse
