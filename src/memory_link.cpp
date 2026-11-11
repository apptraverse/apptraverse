#include "apptraverse/memory_link.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <utility>

#include "apptraverse/no_exceptions.h"

namespace apptraverse {
namespace {

[[noreturn]] void MemoryLinkInvariantFailed(char const* message) {
  std::fputs(message, stderr);
  std::fputc('\n', stderr);
  std::abort();
}

}  // namespace

struct MemoryLinkNetwork::State {
  std::uint64_t now_us{0};
  PacketId next_id{1};
  std::vector<Packet> packets;
  std::map<std::string, IByteLink::Receiver> receivers;
  std::map<std::string, LinkAvailability> availability;

  LinkAvailability Availability(std::string const& endpoint) const {
    auto found = availability.find(endpoint);
    return found == availability.end() ? LinkAvailability::Unknown
                                       : found->second;
  }

  auto FindPacket(PacketId id) {
    return std::find_if(packets.begin(), packets.end(),
                        [id](Packet const& packet) { return packet.id == id; });
  }
};

MemoryLinkNetwork::MemoryLinkNetwork() : state_{std::make_shared<State>()} {}
MemoryLinkNetwork::~MemoryLinkNetwork() = default;

std::uint64_t MemoryLinkNetwork::NowUs() const { return state_->now_us; }

void MemoryLinkNetwork::AdvanceTo(std::uint64_t now_us) {
  if (now_us < state_->now_us) {
    MemoryLinkInvariantFailed("MemoryLink clock cannot go backwards");
  }
  state_->now_us = now_us;
}

std::vector<MemoryLinkNetwork::Packet> MemoryLinkNetwork::PendingPackets() const {
  return state_->packets;
}

bool MemoryLinkNetwork::Deliver(PacketId id) {
  auto packet_it = state_->FindPacket(id);
  if (packet_it == state_->packets.end() || packet_it->due_us > state_->now_us ||
      state_->Availability(packet_it->destination) == LinkAvailability::Offline) {
    return false;
  }
  auto receiver_it = state_->receivers.find(packet_it->destination);
  if (receiver_it == state_->receivers.end() || !receiver_it->second) {
    return false;
  }
  // Remove before dispatch and copy the callback: it may re-enter Send, unbind
  // itself, or destroy its endpoint while handling the value-owned packet.
  auto receiver = receiver_it->second;
  auto packet = std::move(*packet_it);
  state_->packets.erase(packet_it);
  receiver(packet.source, packet.bytes);
  return true;
}

bool MemoryLinkNetwork::Drop(PacketId id) {
  auto packet = state_->FindPacket(id);
  if (packet == state_->packets.end()) {
    return false;
  }
  state_->packets.erase(packet);
  return true;
}

MemoryLinkNetwork::PacketId MemoryLinkNetwork::Duplicate(PacketId id) {
  auto found = state_->FindPacket(id);
  if (found == state_->packets.end()) {
    return 0;
  }
  auto packet = *found;
  packet.id = state_->next_id++;
  auto duplicate_id = packet.id;
  state_->packets.push_back(std::move(packet));
  return duplicate_id;
}

bool MemoryLinkNetwork::Delay(PacketId id, std::uint64_t delay_us) {
  auto packet = state_->FindPacket(id);
  if (packet == state_->packets.end()) {
    return false;
  }
  if (delay_us > std::numeric_limits<std::uint64_t>::max() - state_->now_us) {
    MemoryLinkInvariantFailed("MemoryLink delay exceeds logical clock range");
  }
  packet->due_us = state_->now_us + delay_us;
  return true;
}

void MemoryLinkNetwork::SetAvailability(std::string const& endpoint,
                                        LinkAvailability availability) {
  state_->availability[endpoint] = availability;
}

MemoryLink::MemoryLink(MemoryLinkNetwork& network, std::string endpoint)
    : state_{network.state_}, endpoint_{std::move(endpoint)} {
  if (endpoint_.empty()) {
    MemoryLinkInvariantFailed("MemoryLink endpoint must not be empty");
  }
  auto inserted = state_->receivers.emplace(endpoint_, Receiver{}).second;
  if (!inserted) {
    MemoryLinkInvariantFailed("MemoryLink endpoint is already registered");
  }
}

MemoryLink::~MemoryLink() {
  state_->receivers.erase(endpoint_);
  // Observation from a previous runtime is not inherited by its replacement.
  state_->availability.erase(endpoint_);
}

std::string const& MemoryLink::EndpointId() const { return endpoint_; }

void MemoryLink::SetReceiver(Receiver receiver) {
  auto found = state_->receivers.find(endpoint_);
  if (found == state_->receivers.end()) {
    MemoryLinkInvariantFailed("MemoryLink endpoint is not registered");
  }
  found->second = std::move(receiver);
}

bool MemoryLink::Send(std::string const& destination,
                      std::vector<std::uint8_t> const& bytes) {
  if (state_->Availability(endpoint_) == LinkAvailability::Offline ||
      Availability(destination) == LinkAvailability::Offline) {
    return false;
  }
  state_->packets.push_back({state_->next_id++, endpoint_, destination, bytes,
                             state_->now_us});
  return true;
}

LinkAvailability MemoryLink::Availability(std::string const& destination) const {
  return state_->Availability(destination);
}

}  // namespace apptraverse
