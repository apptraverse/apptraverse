#ifndef APPTRAVERSE_NODE_H_
#define APPTRAVERSE_NODE_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "aether/obj/domain.h"
#include "aether/obj/obj.h"

#include "apptraverse/event.h"

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

  bool ShouldTransferBusinessState() const {
    return io_mode_ == IoMode::kSnapshot || !base_snapshot_id_.IsValid();
  }

  template <typename Concrete>
  bool CaptureBaseSnapshot(ae::ObjId snapshot_id) {
    if (domain == nullptr || !snapshot_id.IsValid() || snapshot_id == obj_id) {
      return false;
    }

    IoModeGuard const guard{io_mode_};
    ae::DomainGraph{domain}.Save(static_cast<Concrete const&>(*this),
                                 snapshot_id);
    return true;
  }

  template <typename Concrete>
  bool CommitEvent(Event::ptr event, ae::ObjId snapshot_id) {
    if (!event) {
      return false;
    }

    bool created_snapshot = false;
    if (!base_snapshot_id_.IsValid()) {
      if (!CaptureBaseSnapshot<Concrete>(snapshot_id)) {
        return false;
      }
      base_snapshot_id_ = snapshot_id;
      created_snapshot = true;
    }

    journal_.push_back(event);
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
    for (auto& event : journal_) {
      event.Load();
      if (!event) {
        return;
      }
      if (!ApplyEvent(*event)) {
        return;
      }
    }
  }

  ae::ObjId base_snapshot_id_;
  std::vector<Event::ptr> journal_;
  IoMode io_mode_{IoMode::kLive};
};

static_assert(ae::HasAnyVersionedSave<Node>::value,
              "Node must expose versioned Save");
static_assert(ae::HasAnyVersionedLoad<Node>::value,
              "Node must expose versioned Load");

}  // namespace apptraverse

#endif  // APPTRAVERSE_NODE_H_
