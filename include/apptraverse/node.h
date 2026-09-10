#ifndef APPTRAVERSE_NODE_H_
#define APPTRAVERSE_NODE_H_

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/event_record.h"
#include "apptraverse/journal_retention_policy.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {

inline std::uint64_t SystemUtcMicros() {
  using Clock = std::chrono::system_clock;
  auto const now = Clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

class Node : public ae::Obj {
  // Version 2: EventRecord includes retained_since_us for age retention.
  // Version 1 journals migrate by stamping retained_since_us at load time.
  APPTRAVERSE_OBJECT(Node, ae::Obj, 2)

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
    std::vector<EventRecordWireV1> wire;
    dnv(base_, base, wire);
    // Conservative migration: unknown age starts at load so age retention
    // does not immediately drop historical records.
    auto const stamp = SystemUtcMicros();
    journal.clear();
    journal.reserve(wire.size());
    for (auto& entry : wire) {
      journal.push_back(EventRecord{
          .event = std::move(entry.event),
          .identity = std::move(entry.identity),
          .order = std::move(entry.order),
          .retained_since_us = stamp,
      });
    }
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    std::vector<EventRecordWireV1> wire;
    wire.reserve(journal.size());
    for (auto const& entry : journal) {
      wire.push_back(EventRecordWireV1{
          .event = entry.event,
          .identity = entry.identity,
          .order = entry.order,
      });
    }
    dnv(base_, base, wire);
  }

  template <typename Dnv>
  void Load(ae::Version<2>, Dnv& dnv) {
    dnv(base_, base, journal);
  }

  template <typename Dnv>
  void Save(ae::Version<2>, Dnv& dnv) const {
    dnv(base_, base, journal);
  }

  Node::ptr base;
  std::vector<EventRecord> journal;

  std::uint64_t Generation() const { return generation_; }

  void AdoptPublishedGeneration(std::uint64_t generation) {
    generation_ = generation;
  }

  void SetJournalRetentionPolicy(JournalRetentionPolicy policy) {
    journal_retention_policy_ = policy;
  }

  JournalRetentionPolicy const& GetJournalRetentionPolicy() const {
    return journal_retention_policy_;
  }

  // Synchronization safety hold. Independent of retention count/age.
  // Future sync frontiers will drive this; bool is enough for this slice.
  void SetJournalCompactionBlocked(bool blocked) {
    journal_compaction_blocked_ = blocked;
  }

  bool IsJournalCompactionBlocked() const {
    return journal_compaction_blocked_;
  }

  // Fold a safe contiguous prefix into base. Housekeeping only: generation and
  // materialized fields are unchanged; notifier is not invoked.
  void CompactJournal(std::uint64_t now_us) { CompactJournalImpl(now_us); }

  // Runtime-only, not reflected. Instance-scoped: each model runtime binds the
  // Nodes it owns. No process-global / thread_local / singleton notifier.
  using MaterializedChangeFn = void (*)(void* ctx, Node& node);

  void BindMaterializedChangeNotifier(void* ctx, MaterializedChangeFn fn) {
    materialized_change_ctx_ = ctx;
    materialized_change_fn_ = fn;
  }

  void ClearMaterializedChangeNotifier() {
    materialized_change_ctx_ = nullptr;
    materialized_change_fn_ = nullptr;
  }

  void CopyMaterializedChangeNotifierFrom(Node const& source) {
    materialized_change_ctx_ = source.materialized_change_ctx_;
    materialized_change_fn_ = source.materialized_change_fn_;
  }

  bool HasMaterializedChangeNotifier() const {
    return materialized_change_fn_ != nullptr;
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
    if (suppress_materialized_change_) {
      return;
    }
    ++generation_;
    if (materialized_change_fn_ != nullptr) {
      materialized_change_fn_(materialized_change_ctx_, *this);
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

  // Longest collapsible prefix: records that neither count nor age retention
  // needs. Empty when compaction is blocked or retention is unlimited.
  std::size_t CollapsiblePrefixCount(std::uint64_t now_us) const {
    if (journal_compaction_blocked_ || journal.empty()) {
      return 0;
    }
    auto const& policy = journal_retention_policy_;
    for (std::size_t i = 0; i < journal.size(); ++i) {
      bool retain = false;
      if (policy.max_events == JournalRetentionPolicy::kUnlimitedEvents) {
        retain = true;
      } else if (journal.size() - i <= policy.max_events) {
        retain = true;
      }
      if (policy.max_age.has_value()) {
        if (journal[i].retained_since_us > now_us) {
          retain = true;
        } else {
          auto const age_us = now_us - journal[i].retained_since_us;
          if (age_us <= static_cast<std::uint64_t>(policy.max_age->count())) {
            retain = true;
          }
        }
      }
      if (retain) {
        return i;
      }
    }
    return journal.size();
  }

  template <typename ConcreteNode>
  void CompactJournalInto(ConcreteNode& target, std::uint64_t now_us) {
    auto const collapse = CollapsiblePrefixCount(now_us);
    if (collapse == 0) {
      return;
    }

    auto const saved_generation = generation_;
    std::vector<EventRecord> retained(
        journal.begin() + static_cast<std::ptrdiff_t>(collapse),
        journal.end());

    if (retained.empty()) {
      // Live fields already equal old base + the whole journal. Snapshot them
      // as the new base. Rebuilding through storage would reload the old
      // base into this object and is unnecessary when nothing is retained.
      journal.clear();
      CaptureBaseStateInto(target);
      generation_ = saved_generation;
      applied_journal_size_ = kJournalFullyMaterialized;
      return;
    }

    journal.erase(journal.begin() + static_cast<std::ptrdiff_t>(collapse),
                  journal.end());

    suppress_materialized_change_ = true;
    RebuildFromBaseAndReplay(target);
    journal.clear();
    CaptureBaseStateInto(target);
    journal = std::move(retained);
    ReplayJournal();
    suppress_materialized_change_ = false;
    generation_ = saved_generation;
    applied_journal_size_ = kJournalFullyMaterialized;
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

    if (record.retained_since_us == 0) {
      record.retained_since_us = SystemUtcMicros();
    }

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
        .retained_since_us = SystemUtcMicros(),
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
        .retained_since_us = SystemUtcMicros(),
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

  virtual void CompactJournalImpl(std::uint64_t now_us) {
    (void)now_us;
    assert(false && "Concrete Node must inherit through NodeFor");
  }

  static constexpr std::size_t kJournalFullyMaterialized =
      (std::numeric_limits<std::size_t>::max)();

  void* materialized_change_ctx_{nullptr};
  MaterializedChangeFn materialized_change_fn_{nullptr};

  std::uint64_t generation_{1};
  std::size_t applied_journal_size_{kJournalFullyMaterialized};
  JournalRetentionPolicy journal_retention_policy_{};
  bool journal_compaction_blocked_{false};
  bool suppress_materialized_change_{false};
};

// Ordered unique dirty set for GUI publication. First-dirty order is preserved;
// membership prevents duplicate entries while Events keep coalescing into one
// pending Node until published.
struct PendingDirtyNodes {
  void Note(Node& node) {
    if (membership.insert(&node).second) {
      ordered.push_back(&node);
    }
  }

  [[nodiscard]] bool empty() const { return ordered.empty(); }

  void Clear() {
    ordered.clear();
    membership.clear();
  }

  Node* PopFront() {
    assert(!ordered.empty());
    Node* const node = ordered.front();
    ordered.erase(ordered.begin());
    membership.erase(node);
    return node;
  }

  std::vector<Node*> ordered;
  std::unordered_set<Node*> membership;
};

inline void PendingDirtyNodesNotify(void* ctx, Node& node) {
  static_cast<PendingDirtyNodes*>(ctx)->Note(node);
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_NODE_H_
