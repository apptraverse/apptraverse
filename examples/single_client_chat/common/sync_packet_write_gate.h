#ifndef APPTRAVERSE_SYNC_PACKET_WRITE_GATE_H_
#define APPTRAVERSE_SYNC_PACKET_WRITE_GATE_H_

#include <chrono>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "aether/clock.h"
#include "aether/obj/obj_id.h"

namespace apptraverse::chat {

// Per-logical-packet physical send cooldown (runtime only, not persisted).
// First send is immediate; after each physical send the next is allowed only
// after packet_retry_interval. Application ACK removes the slot via Forget /
// RetainOnly — no transport WriteAction completion is involved.
class SyncPacketWriteGate {
 public:
  explicit SyncPacketWriteGate(
      std::chrono::milliseconds packet_retry_interval =
          std::chrono::milliseconds{2000})
      : packet_retry_interval_{packet_retry_interval} {}

  void set_packet_retry_interval(std::chrono::milliseconds interval) {
    packet_retry_interval_ = interval;
  }

  // Returns true when a physical send may start now. On success records the
  // next allowed send time and increments attempt_count.
  bool TryBegin(ae::ObjId packet_id, ae::TimePoint now);

  bool Has(ae::ObjId packet_id) const;
  std::size_t size() const { return slots_.size(); }
  std::uint64_t attempt_count(ae::ObjId packet_id) const;
  std::optional<ae::TimePoint> next_send_time(ae::ObjId packet_id) const;

  void Forget(ae::ObjId packet_id);
  void RetainOnly(std::vector<ae::ObjId> const& keep_ids);
  void Clear();

 private:
  struct Slot {
    ae::TimePoint next_physical_send_time{};
    std::uint64_t attempt_count{0};
    bool has_sent{false};
  };

  Slot* Find(ae::ObjId packet_id);
  Slot const* Find(ae::ObjId packet_id) const;

  std::chrono::milliseconds packet_retry_interval_;
  std::unordered_map<std::uint32_t, Slot> slots_;
};

inline SyncPacketWriteGate::Slot* SyncPacketWriteGate::Find(
    ae::ObjId packet_id) {
  auto it = slots_.find(packet_id.id());
  return it == slots_.end() ? nullptr : &it->second;
}

inline SyncPacketWriteGate::Slot const* SyncPacketWriteGate::Find(
    ae::ObjId packet_id) const {
  auto it = slots_.find(packet_id.id());
  return it == slots_.end() ? nullptr : &it->second;
}

inline bool SyncPacketWriteGate::TryBegin(ae::ObjId packet_id,
                                          ae::TimePoint now) {
  auto& slot = slots_[packet_id.id()];
  if (slot.has_sent && now < slot.next_physical_send_time) {
    return false;
  }
  slot.has_sent = true;
  slot.next_physical_send_time = now + packet_retry_interval_;
  ++slot.attempt_count;
  return true;
}

inline bool SyncPacketWriteGate::Has(ae::ObjId packet_id) const {
  return Find(packet_id) != nullptr;
}

inline std::uint64_t SyncPacketWriteGate::attempt_count(
    ae::ObjId packet_id) const {
  auto const* slot = Find(packet_id);
  return slot == nullptr ? 0 : slot->attempt_count;
}

inline std::optional<ae::TimePoint> SyncPacketWriteGate::next_send_time(
    ae::ObjId packet_id) const {
  auto const* slot = Find(packet_id);
  if (slot == nullptr || !slot->has_sent) {
    return std::nullopt;
  }
  return slot->next_physical_send_time;
}

inline void SyncPacketWriteGate::Forget(ae::ObjId packet_id) {
  slots_.erase(packet_id.id());
}

inline void SyncPacketWriteGate::RetainOnly(
    std::vector<ae::ObjId> const& keep_ids) {
  std::unordered_map<std::uint32_t, bool> keep;
  keep.reserve(keep_ids.size());
  for (auto const& id : keep_ids) {
    keep[id.id()] = true;
  }
  for (auto it = slots_.begin(); it != slots_.end();) {
    if (keep.find(it->first) == keep.end()) {
      it = slots_.erase(it);
    } else {
      ++it;
    }
  }
}

inline void SyncPacketWriteGate::Clear() { slots_.clear(); }

}  // namespace apptraverse::chat

#endif  // APPTRAVERSE_SYNC_PACKET_WRITE_GATE_H_
