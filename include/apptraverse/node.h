#ifndef APPTRAVERSE_NODE_H_
#define APPTRAVERSE_NODE_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "aether/obj/domain.h"
#include "aether/obj/obj.h"

#include "apptraverse/event.h"
#include "apptraverse/event_record.h"

namespace apptraverse {

class Node : public ae::Obj {
  AE_OBJECT(Node, ae::Obj, 0)

 protected:
  Node() = default;

 public:
  explicit Node(ae::ObjProp prop) : ae::Obj{prop} {}

  AE_OBJECT_REFLECT()

  template <typename Dnv>
  void Load(CurrentVersion, Dnv& dnv) {
    dnv(base_);
    if (io_mode_ == IoMode::kLive) {
      dnv(base_snapshot_id_, journal_);
    }
  }

  template <typename Dnv>
  void Save(CurrentVersion, Dnv& dnv) const {
    dnv(base_);
    if (io_mode_ == IoMode::kLive) {
      dnv(base_snapshot_id_, journal_);
    }
  }

 protected:
  bool ApplyEvent(Event const& event) { return event.ApplyTo(*this); }

  ae::ObjId BaseSnapshotId() const { return base_snapshot_id_; }
  std::size_t JournalSize() const { return journal_.size(); }

  /**
   * \brief Next local sequence for a newly committed record.
   *
   * Invariant: successfully committed records are numbered 1..N in commit
   * order with no gaps. Failed commits remove the tentative record and do not
   * consume a sequence number, so the next sequence is always
   * journal_.size() + 1. No separate persisted counter is stored.
   */
  std::uint64_t NextLocalSequence() const {
    return static_cast<std::uint64_t>(journal_.size()) + 1U;
  }

  std::uint64_t JournalSequenceAt(std::size_t index) const {
    if (index >= journal_.size()) {
      return 0;
    }
    return journal_[index].sequence();
  }

  EventDeliveryState JournalDeliveryStateAt(std::size_t index) const {
    if (index >= journal_.size()) {
      return EventDeliveryState::kPending;
    }
    return journal_[index].delivery_state();
  }

  Event::ptr JournalEventAt(std::size_t index) const {
    if (index >= journal_.size()) {
      return {};
    }
    return journal_[index].event();
  }

  ae::ObjId JournalEventIdAt(std::size_t index) const {
    auto event = JournalEventAt(index);
    if (!event.is_valid()) {
      return {};
    }
    return event.id();
  }

  bool ShouldTransferBusinessState() const {
    return io_mode_ == IoMode::kSnapshot || !base_snapshot_id_.IsValid();
  }

  bool CaptureBaseSnapshot(ae::ObjId snapshot_id) {
    if (domain == nullptr || !obj_id.IsValid() || !snapshot_id.IsValid() ||
        snapshot_id == obj_id) {
      return false;
    }

    IoModeGuard const guard{io_mode_};
    Node::ptr self = Node::ptr::MakeFromThis(this);
    ae::Obj::ptr root = self;
    ae::DomainGraph{domain}.SaveRootImpl(root.Load(), snapshot_id);
    return true;
  }

  bool CommitEvent(Event::ptr event, ae::ObjId snapshot_id) {
    if (!event || !event.is_valid()) {
      return false;
    }
    event.Load();
    if (!event) {
      return false;
    }

    bool created_snapshot = false;
    if (!base_snapshot_id_.IsValid()) {
      if (!CaptureBaseSnapshot(snapshot_id)) {
        return false;
      }
      base_snapshot_id_ = snapshot_id;
      created_snapshot = true;
    }

    auto const sequence = NextLocalSequence();
    journal_.emplace_back(event, sequence, EventDeliveryState::kPending);
    if (!ApplyEvent(*event)) {
      journal_.pop_back();
      if (created_snapshot) {
        base_snapshot_id_.Invalidate();
      }
      return false;
    }

    return true;
  }

  template <typename Concrete>
  void FinishLoadIfMostDerived() {
    if (io_mode_ != IoMode::kLive) {
      return;
    }
    if (GetClassId() != Concrete::kClassId) {
      return;
    }
    if (!base_snapshot_id_.IsValid()) {
      return;
    }

    {
      IoModeGuard const guard{io_mode_};
      ae::DomainGraph{domain}.Load(static_cast<Concrete&>(*this),
                                   base_snapshot_id_);
    }

    ReplayJournal();
  }

 private:
  enum class IoMode : std::uint8_t {
    kLive,
    kSnapshot,
  };

  class IoModeGuard {
   public:
    explicit IoModeGuard(IoMode& mode)
        : mode_{mode}, previous_{mode} {
      mode_ = IoMode::kSnapshot;
    }

    ~IoModeGuard() { mode_ = previous_; }

    IoModeGuard(IoModeGuard const&) = delete;
    IoModeGuard& operator=(IoModeGuard const&) = delete;

   private:
    IoMode& mode_;
    IoMode previous_;
  };

  void ReplayJournal() {
    for (auto& record : journal_) {
      record.event_.Load();
      if (!record.event_) {
        return;
      }
      if (!ApplyEvent(*record.event_)) {
        return;
      }
    }
  }

  ae::ObjId base_snapshot_id_;
  std::vector<EventRecord> journal_;
  IoMode io_mode_{IoMode::kLive};
};

static_assert(ae::HasAnyVersionedSave<Node>::value,
              "Node must expose versioned Save");
static_assert(ae::HasAnyVersionedLoad<Node>::value,
              "Node must expose versioned Load");

}  // namespace apptraverse

#endif  // APPTRAVERSE_NODE_H_
