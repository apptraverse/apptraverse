#ifndef APPTRAVERSE_EXAMPLES_AETHER_P2P_WRITE_GATE_H_
#define APPTRAVERSE_EXAMPLES_AETHER_P2P_WRITE_GATE_H_

#include <cstdint>
#include <map>
#include <utility>

namespace apptraverse::examples {

// One in-flight write per (peer, packet_id). Header-only and independent of
// P2pStream / Aether so it can be unit-tested with plain comparable ids.
template <typename PeerId, typename PacketId>
class SyncWriteGate {
 public:
  // Returns true and assigns a new attempt when no write is in flight.
  // Returns false and sets attempt_out to the active attempt otherwise.
  bool TryBegin(PeerId const& peer, PacketId const& packet_id,
                std::uint32_t& attempt_out);

  // Unlocks only when `attempt` is the active in-flight attempt.
  void Complete(PeerId const& peer, PacketId const& packet_id,
                std::uint32_t attempt);

  bool IsInFlight(PeerId const& peer, PacketId const& packet_id) const;

  void ClearPeer(PeerId const& peer);
  void Clear();

 private:
  using Key = std::pair<PeerId, PacketId>;

  struct Slot {
    std::uint32_t next_attempt{1};
    std::uint32_t in_flight_attempt{0};
  };

  std::map<Key, Slot> slots_;
};

template <typename PeerId, typename PacketId>
bool SyncWriteGate<PeerId, PacketId>::TryBegin(PeerId const& peer,
                                               PacketId const& packet_id,
                                               std::uint32_t& attempt_out) {
  auto& slot = slots_[Key{peer, packet_id}];
  if (slot.in_flight_attempt != 0) {
    attempt_out = slot.in_flight_attempt;
    return false;
  }
  attempt_out = slot.next_attempt++;
  slot.in_flight_attempt = attempt_out;
  return true;
}

template <typename PeerId, typename PacketId>
void SyncWriteGate<PeerId, PacketId>::Complete(PeerId const& peer,
                                               PacketId const& packet_id,
                                               std::uint32_t attempt) {
  auto const it = slots_.find(Key{peer, packet_id});
  if (it == slots_.end()) {
    return;
  }
  if (it->second.in_flight_attempt == attempt) {
    it->second.in_flight_attempt = 0;
  }
}

template <typename PeerId, typename PacketId>
bool SyncWriteGate<PeerId, PacketId>::IsInFlight(
    PeerId const& peer, PacketId const& packet_id) const {
  auto const it = slots_.find(Key{peer, packet_id});
  return it != slots_.end() && it->second.in_flight_attempt != 0;
}

template <typename PeerId, typename PacketId>
void SyncWriteGate<PeerId, PacketId>::ClearPeer(PeerId const& peer) {
  for (auto it = slots_.begin(); it != slots_.end();) {
    if (it->first.first == peer) {
      it = slots_.erase(it);
    } else {
      ++it;
    }
  }
}

template <typename PeerId, typename PacketId>
void SyncWriteGate<PeerId, PacketId>::Clear() {
  slots_.clear();
}

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_AETHER_P2P_WRITE_GATE_H_
