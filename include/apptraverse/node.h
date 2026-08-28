#ifndef APPTRAVERSE_NODE_H_
#define APPTRAVERSE_NODE_H_

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include "aether/obj/domain.h"
#include "aether/obj/obj.h"

#include "apptraverse/event_record.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {

inline std::uint64_t SystemUtcMicros() {
  using Clock = std::chrono::system_clock;
  auto const now = Clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

class Node : public ae::Obj {
  // Version 1: EventRecord uses SharedEventId/Order (no timestamp_us packing).
  APPTRAVERSE_OBJECT(Node, ae::Obj, 1)

 protected:
  Node() = default;

 public:
  explicit Node(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(base), AE_MMBR(journal))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv&) {
    throw std::runtime_error(
        "AppTraverse Node journal v0 (timestamp_us) is not supported; "
        "re-distill with a fresh state dir");
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    dnv(base_, base, journal);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    dnv(base_, base, journal);
  }

  Node::ptr base;
  std::vector<EventRecord> journal;

  std::uint64_t Generation() const { return generation_; }

  void AdoptPublishedGeneration(std::uint64_t generation) {
    generation_ = generation;
  }

  static void SetMaterializedChangeNotifier(
      std::function<void(Node&)> notifier) {
    materialized_change_notifier_ = std::move(notifier);
  }

  virtual void OnLoad() {}

  virtual void Update(std::chrono::steady_clock::time_point now) {
    (void)now;
  }

  void EnsureCurrentGeneration() {
    if (applied_journal_size_ == kJournalFullyMaterialized) {
      applied_journal_size_ = journal.size();
      return;
    }
    while (applied_journal_size_ < journal.size()) {
      auto const index = applied_journal_size_++;
      auto const& record = journal[index];
      assert(record.event.is_valid());
      assert(record.event.is_loaded());
      assert(record.event->CanApplyTo(*this));
      ApplyEvent(*record.event);
    }
  }

  void CaptureBaseState() { CaptureBaseStateImpl(); }

  void Commit(Event::ptr event) { CommitImpl(std::move(event)); }

 protected:
  void ApplyEvent(Event const& event) { event.ApplyTo(*this); }

  void NoteMaterializedChange() {
    ++generation_;
    if (materialized_change_notifier_) {
      materialized_change_notifier_(*this);
    }
  }

  void ReplayJournal() {
    applied_journal_size_ = 0;
    EnsureCurrentGeneration();
  }

  template <typename ConcreteNode>
  void RebuildFromBaseAndReplay(ConcreteNode& target) {
    auto owner_id = obj_id;
    auto saved_base = base;
    auto saved_journal = journal;
    ae::DomainGraph graph{domain};
    graph.Load(target, saved_base.id());
    obj_id = owner_id;
    base = saved_base;
    journal = std::move(saved_journal);
    generation_ = 1;
    ReplayJournal();
  }

  template <typename ConcreteNode>
  void CaptureBaseStateInto(ConcreteNode& target) {
    assert(domain != nullptr);
    assert(base.is_valid());
    assert(base.is_loaded());
    assert(journal.empty());
    assert(base.id() != obj_id);

    auto owner_id = obj_id;
    auto saved_base = base;
    auto saved_journal = journal;

    base = {};
    journal.clear();

    ae::DomainGraph save_graph{domain};
    bool const owner_marked =
        save_graph.cycle_detector.Add(ConcreteNode::kClassId, owner_id);
    assert(owner_marked);
    save_graph.Save(target, saved_base.id());

    obj_id = owner_id;
    base = saved_base;
    journal = std::move(saved_journal);

    auto& concrete_base = static_cast<ConcreteNode&>(*base);
    ae::DomainGraph load_graph{domain};
    load_graph.Load(concrete_base, base.id());
  }

  template <typename ConcreteNode>
  void InsertEvent(ConcreteNode& target, EventRecord record) {
    assert(domain != nullptr);
    assert(base.is_valid());
    assert(base.is_loaded());
    assert(record.event.is_valid());
    assert(record.event.is_loaded());
    assert(record.event.domain() == domain);
    assert(record.order.lamport != 0 || !record.order.origin_uid.empty() ||
           record.order.origin_sequence != 0);

    for (auto const& existing : journal) {
      assert(!(existing.order == record.order) && "duplicate EventRecord order");
      if (record.HasSharedIdentity() && existing.HasSharedIdentity()) {
        assert(!(existing.identity == record.identity) &&
               "duplicate SharedEventId");
      }
    }

    auto position = std::lower_bound(journal.begin(), journal.end(), record,
                                     EventRecordOrderLess);

    bool const appended = position == journal.end();
    journal.insert(position, std::move(record));

    if (appended) {
      if (applied_journal_size_ == kJournalFullyMaterialized) {
        applied_journal_size_ = journal.size() - 1;
      }
      EnsureCurrentGeneration();
    } else {
      RebuildFromBaseAndReplay(target);
    }
  }

  // Non-shared local commit: monotonic local order (empty identity).
  template <typename ConcreteNode>
  void CommitInto(ConcreteNode& target, Event::ptr event) {
    assert(event.is_valid());
    assert(event.is_loaded());
    assert(event->CanApplyTo(target));

    std::uint64_t lamport = SystemUtcMicros();
    if (!journal.empty()) {
      auto const& last = journal.back().order;
      if (lamport <= last.lamport) {
        lamport = last.lamport + 1;
      }
    }

    EventRecord record{
        .event = std::move(event),
        .identity = {},
        .order =
            SharedEventOrder{
                .lamport = lamport,
                .origin_uid = {},
                .origin_sequence = 0,
            },
    };
    InsertEvent(target, std::move(record));
  }

  // Shared commit: EventRecord is inserted with the canonical SharedEventOrder.
  template <typename ConcreteNode>
  void CommitSharedInto(ConcreteNode& target, Event::ptr event,
                        SharedEventId identity, SharedEventOrder order) {
    assert(event.is_valid());
    assert(event.is_loaded());
    assert(event->CanApplyTo(target));
    assert(!identity.origin_uid.empty());
    assert(order.origin_uid == identity.origin_uid);
    assert(order.origin_sequence == identity.origin_sequence);

    EventRecord record{
        .event = std::move(event),
        .identity = std::move(identity),
        .order = std::move(order),
    };
    InsertEvent(target, std::move(record));
  }

 private:
  virtual void CaptureBaseStateImpl() {
    assert(false && "Concrete Node must inherit through NodeFor");
  }

  virtual void CommitImpl(Event::ptr event) {
    (void)event;
    assert(false && "Concrete Node must inherit through NodeFor");
  }

  static constexpr std::size_t kJournalFullyMaterialized =
      (std::numeric_limits<std::size_t>::max)();

  static inline std::function<void(Node&)> materialized_change_notifier_{};

  std::uint64_t generation_{1};
  std::size_t applied_journal_size_{kJournalFullyMaterialized};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_NODE_H_
