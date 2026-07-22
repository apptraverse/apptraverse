#ifndef APPTRAVERSE_NODE_H_
#define APPTRAVERSE_NODE_H_

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "aether/obj/domain.h"
#include "aether/obj/obj.h"

#include "apptraverse/event.h"
#include "apptraverse/event_record.h"
#include "apptraverse/replica_id.h"

namespace apptraverse {

class JournalSynchronizer;

class Node : public ae::Obj {
  AE_OBJECT(Node, ae::Obj, 0)

  friend class JournalSynchronizer;

 protected:
  Node() = default;

 public:
  explicit Node(ae::ObjProp prop) : ae::Obj{prop} {}

  AE_OBJECT_REFLECT()

  template <typename Dnv>
  void Load(CurrentVersion, Dnv& dnv) {
    dnv(base_);
    if (io_mode_ == IoMode::kLive) {
      dnv(base_snapshot_id_, replica_id_, next_local_sequence_, logical_clock_,
          journal_);
    }
  }

  template <typename Dnv>
  void Save(CurrentVersion, Dnv& dnv) const {
    dnv(base_);
    if (io_mode_ == IoMode::kLive) {
      dnv(base_snapshot_id_, replica_id_, next_local_sequence_, logical_clock_,
          journal_);
    }
  }

 protected:
  void ApplyEvent(Event const& event) { event.ApplyTo(*this); }

  ae::ObjId BaseSnapshotId() const { return base_snapshot_id_; }
  std::size_t JournalSize() const { return journal_.size(); }
  ReplicaId replica_id() const { return replica_id_; }
  std::uint64_t next_local_sequence() const { return next_local_sequence_; }
  std::uint64_t logical_clock() const { return logical_clock_; }

  EventRecord const& JournalRecordAt(std::size_t index) const {
    assert(index < journal_.size());
    return journal_[index];
  }

  EventIdentity JournalIdentityAt(std::size_t index) const {
    return JournalRecordAt(index).identity();
  }

  std::uint64_t JournalLogicalTimeAt(std::size_t index) const {
    return JournalRecordAt(index).logical_time();
  }

  EventDeliveryState JournalDeliveryStateAt(std::size_t index) const {
    return JournalRecordAt(index).delivery_state();
  }

  Event::ptr JournalEventAt(std::size_t index) const {
    return JournalRecordAt(index).event();
  }

  ae::ObjId JournalEventIdAt(std::size_t index) const {
    auto event = JournalEventAt(index);
    assert(event.is_valid());
    return event.id();
  }

  void InitializeReplica(ReplicaId replica_id) {
    assert(replica_id.IsValid());
    assert(journal_.empty());
    assert(!replica_id_.IsValid() || replica_id_ == replica_id);
    replica_id_ = replica_id;
  }

  bool ShouldTransferBusinessState() const {
    return io_mode_ == IoMode::kSnapshot || !base_snapshot_id_.IsValid();
  }

  void CaptureBaseSnapshot(ae::ObjId snapshot_id) {
    assert(domain != nullptr);
    assert(obj_id.IsValid());
    assert(snapshot_id.IsValid());
    assert(snapshot_id != obj_id);

    IoModeGuard const guard{io_mode_};
    Node::ptr self = Node::ptr::MakeFromThis(this);
    ae::Obj::ptr root = self;
    ae::DomainGraph{domain}.SaveRootImpl(root.Load(), snapshot_id);
  }

  void CommitEvent(Event::ptr event, ae::ObjId snapshot_id) {
    assert(event && event.is_valid());
    event.Load();
    assert(event);
    assert(replica_id_.IsValid());
    assert(domain != nullptr);

    if (!base_snapshot_id_.IsValid()) {
      CaptureBaseSnapshot(snapshot_id);
      base_snapshot_id_ = snapshot_id;
    }

    ++logical_clock_;
    EventIdentity const identity{replica_id_, next_local_sequence_};
    ++next_local_sequence_;
    // next_local_sequence_ always remains greater than every local sequence
    // previously issued by this replica.
    journal_.emplace_back(std::move(event), identity, logical_clock_,
                          EventDeliveryState::kPending);
    ApplyEvent(*journal_.back().event_);
  }

  void AcceptRemoteEvent(EventRecord record) {
    assert(record.identity().IsValid());
    assert(record.origin() != replica_id_);
    assert(record.event().is_valid());

    if (FindRecordIndex(record.identity()) < journal_.size()) {
      return;
    }

    if (record.logical_time() > logical_clock_) {
      logical_clock_ = record.logical_time();
    }

    record.delivery_state_ = EventDeliveryState::kConfirmed;
    journal_.push_back(std::move(record));
    std::sort(journal_.begin(), journal_.end(), EventRecord::OrderBefore);
    RebuildAfterJournalChange();
  }

  void MarkSent(EventIdentity const& identity) {
    auto const index = FindRecordIndex(identity);
    assert(index < journal_.size());
    auto& state = journal_[index].delivery_state_;
    assert(state == EventDeliveryState::kPending ||
           state == EventDeliveryState::kSent ||
           state == EventDeliveryState::kConfirmed);
    if (state == EventDeliveryState::kPending) {
      state = EventDeliveryState::kSent;
    }
  }

  void MarkConfirmed(EventIdentity const& identity) {
    auto const index = FindRecordIndex(identity);
    assert(index < journal_.size());
    auto& state = journal_[index].delivery_state_;
    assert(state == EventDeliveryState::kPending ||
           state == EventDeliveryState::kSent ||
           state == EventDeliveryState::kConfirmed);
    state = EventDeliveryState::kConfirmed;
  }

  template <typename Concrete>
  void RestoreSnapshotAndReplay() {
    assert(domain != nullptr);
    assert(base_snapshot_id_.IsValid());
    {
      IoModeGuard const guard{io_mode_};
      ae::DomainGraph{domain}.Load(static_cast<Concrete&>(*this),
                                   base_snapshot_id_);
    }
    ReplayJournal();
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
    RestoreSnapshotAndReplay<Concrete>();
  }

 private:
  enum class IoMode : std::uint8_t {
    kLive,
    kSnapshot,
  };

  class IoModeGuard {
   public:
    explicit IoModeGuard(IoMode& mode) : mode_{mode}, previous_{mode} {
      mode_ = IoMode::kSnapshot;
    }

    ~IoModeGuard() { mode_ = previous_; }

    IoModeGuard(IoModeGuard const&) = delete;
    IoModeGuard& operator=(IoModeGuard const&) = delete;

   private:
    IoMode& mode_;
    IoMode previous_;
  };

  std::size_t FindRecordIndex(EventIdentity const& identity) const {
    for (std::size_t i = 0; i < journal_.size(); ++i) {
      if (journal_[i].identity() == identity) {
        return i;
      }
    }
    return journal_.size();
  }

  void ReplayJournal() {
    for (auto& record : journal_) {
      record.event_.Load();
      assert(record.event_);
      ApplyEvent(*record.event_);
    }
  }

  virtual void RebuildAfterJournalChange() {
    assert(false && "most-derived Node must rebuild after journal changes");
  }

  ae::ObjId base_snapshot_id_;
  ReplicaId replica_id_{};
  std::uint64_t next_local_sequence_{1};
  std::uint64_t logical_clock_{0};
  std::vector<EventRecord> journal_;
  IoMode io_mode_{IoMode::kLive};
};

static_assert(ae::HasAnyVersionedSave<Node>::value,
              "Node must expose versioned Save");
static_assert(ae::HasAnyVersionedLoad<Node>::value,
              "Node must expose versioned Load");

}  // namespace apptraverse

#endif  // APPTRAVERSE_NODE_H_
