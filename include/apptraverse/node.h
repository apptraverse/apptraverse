#ifndef APPTRAVERSE_NODE_H_
#define APPTRAVERSE_NODE_H_

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/event_record.h"
#include "apptraverse/journal_retention_policy.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {

inline std::uint64_t SystemUtcMicros() {
  using Clock = std::chrono::system_clock;
  auto const now = Clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

// Written at the head of every Node payload and checked before the journal is
// decoded. A Node's fields are serialized into the storage layer of the most
// derived class, so the Node version is not part of the storage key of any
// concrete Node and cannot by itself separate an old journal from a current
// one. Journals up to Node v2 ordered EventRecords by
// (lamport, origin_uid, origin_sequence); read as the current layout those
// bytes decode into a journal of broken Event references instead of failing,
// so the format is stated explicitly and obsolete state is rejected.
inline constexpr std::uint64_t kNodeJournalFormat = 0x41545F4A524E4C33ULL;

class Node : public ae::Obj {
  // Version 3: SharedEventOrder is timestamp_us only.
  APPTRAVERSE_OBJECT(Node, ae::Obj, 3)

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
  void Load(ae::Version<1>, Dnv&) {
    throw std::runtime_error(
        "AppTraverse Node journal v1 ordered by (lamport, origin_uid, "
        "origin_sequence); re-distill with a fresh state dir");
  }

  template <typename Dnv>
  void Load(ae::Version<2>, Dnv&) {
    throw std::runtime_error(
        "AppTraverse Node journal v2 ordered by (lamport, origin_uid, "
        "origin_sequence); re-distill with a fresh state dir");
  }

  template <typename Dnv>
  void Load(ae::Version<3>, Dnv& dnv) {
    std::uint64_t format = 0;
    dnv(format);
    if (format != kNodeJournalFormat) {
      throw std::runtime_error(
          "AppTraverse Node journal predates timestamp-only Event order; "
          "re-distill with a fresh state dir");
    }
    dnv(base_, base, journal);
  }

  template <typename Dnv>
  void Save(ae::Version<3>, Dnv& dnv) const {
    std::uint64_t const format = kNodeJournalFormat;
    dnv(format, base_, base, journal);
  }

  SharedPtr<Node> base;
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

  bool TryEnsureCurrentGeneration() {
    if (applied_journal_size_ == kJournalFullyMaterialized) {
      applied_journal_size_ = journal.size();
      return true;
    }
    while (applied_journal_size_ < journal.size()) {
      auto const& record = journal[applied_journal_size_];
      if (!record.event.is_valid() || !record.event.is_loaded()) {
        return false;
      }
      if (!record.event->CanApplyTo(*this)) {
        return false;
      }
      ApplyEvent(*record.event);
      ++applied_journal_size_;
    }
    return true;
  }

  void EnsureCurrentGeneration() {
    bool const ok = TryEnsureCurrentGeneration();
    assert(ok && "EnsureCurrentGeneration invariant broken");
    (void)ok;
  }

  void CaptureBaseState() { CaptureBaseStateImpl(); }

  void Commit(Event::ptr event) { CommitImpl(std::move(event)); }

  // Rebuild materialized state from base and replay the journal.
  // Note: Speculative replay is intended for disposable scratch copies only.
  // If historical replay fails, the scratch object is left partially modified
  // and must not be reused. Network admission discards the scratch graph on failure.
  bool TryReplayFromBase() { return TryReplayFromBaseImpl(); }

  void ReplayFromBase() {
    bool const ok = TryReplayFromBase();
    assert(ok && "ReplayFromBase invariant broken");
    (void)ok;
  }

  // Insert a remotely originated shared Event at its own identity and
  // timestamp. Dispatches to the most-derived NodeFor so Apply sees the
  // concrete Node.
  // Note: Speculative insertion is intended for disposable scratch copies only.
  // If historical replay fails, the scratch object is left partially modified
  // and must not be reused. Network admission discards the scratch graph on failure.
  bool TryInsertShared(Event::ptr event, SharedEventId identity,
                       SharedEventOrder order) {
    return TryInsertSharedImpl(std::move(event), std::move(identity),
                               std::move(order));
  }

  void InsertShared(Event::ptr event, SharedEventId identity,
                    SharedEventOrder order) {
    bool const ok = TryInsertShared(std::move(event), std::move(identity),
                                    std::move(order));
    assert(ok && "InsertShared invariant broken");
    (void)ok;
  }

  EventRecord const* FindSharedEvent(SharedEventId const& identity) const {
    if (identity.origin_uid.empty()) {
      return nullptr;
    }
    for (auto const& record : journal) {
      if (record.HasSharedIdentity() && record.identity == identity) {
        return &record;
      }
    }
    return nullptr;
  }

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

  bool TryReplayJournal() {
    applied_journal_size_ = 0;
    return TryEnsureCurrentGeneration();
  }

  void ReplayJournal() {
    bool const ok = TryReplayJournal();
    assert(ok && "ReplayJournal invariant broken");
    (void)ok;
  }

  // Local-persistent fields (e.g. SharedNode LocalPtr sync metadata) must
  // survive mid-journal RebuildFromBaseAndReplay. Base snapshots may predate
  // those fields; Load from base must not discard the live local state.
  virtual void StashLocalPersistentAcrossRebuild() {}
  virtual void RestoreLocalPersistentAcrossRebuild() {}

  template <typename ConcreteNode>
  bool TryRebuildFromBaseAndReplay(ConcreteNode& target) {
    auto owner_id = obj_id;
    auto saved_base = base;
    auto saved_journal = journal;
    target.StashLocalPersistentAcrossRebuild();
    ae::DomainGraph graph{domain};
    graph.Load(target, saved_base.id());
    obj_id = owner_id;
    base = saved_base;
    journal = std::move(saved_journal);
    target.RestoreLocalPersistentAcrossRebuild();
    generation_ = 1;
    return TryReplayJournal();
  }

  template <typename ConcreteNode>
  void RebuildFromBaseAndReplay(ConcreteNode& target) {
    bool const ok = TryRebuildFromBaseAndReplay(target);
    assert(ok && "RebuildFromBaseAndReplay invariant broken");
    (void)ok;
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

  // Speculative insertion is for disposable scratch copies only.
  // If historical replay fails, the scratch object is left partially modified
  // and must be discarded. Network admission discards the scratch graph on failure.
  template <typename ConcreteNode>
  bool TryInsertEvent(ConcreteNode& target, EventRecord record) {
    if (domain == nullptr || !base.is_valid() || !base.is_loaded() ||
        !record.event.is_valid() || !record.event.is_loaded() ||
        record.event.domain() != domain || record.order.timestamp_us == 0) {
      return false;
    }

    if (record.retained_since_us == 0) {
      record.retained_since_us = SystemUtcMicros();
    }

    // Identity is what must be unique. Two different Events sharing a
    // timestamp is an accepted, unresolved case, not an error.
    for (auto const& existing : journal) {
      if (record.HasSharedIdentity() && existing.HasSharedIdentity()) {
        if (existing.identity == record.identity) {
          return false;
        }
      }
    }

    // A record with a timestamp already present lands before the records that
    // carry it. That is what lower_bound does on this replica, not an agreed
    // rule: where equal timestamps end up relative to each other is still an
    // open question, and another replica may place them the other way round.
    auto position = std::lower_bound(journal.begin(), journal.end(), record,
                                     EventRecordOrderLess);

    bool const appended = position == journal.end();
    journal.insert(position, std::move(record));

    if (appended) {
      if (applied_journal_size_ == kJournalFullyMaterialized) {
        applied_journal_size_ = journal.size() - 1;
      }
      if (!TryEnsureCurrentGeneration()) {
        journal.pop_back();
        applied_journal_size_ = journal.size();
        return false;
      }
      return true;
    } else {
      return TryRebuildFromBaseAndReplay(target);
    }
  }

  template <typename ConcreteNode>
  void InsertEvent(ConcreteNode& target, EventRecord record) {
    bool const ok = TryInsertEvent(target, std::move(record));
    assert(ok && "InsertEvent invariant broken");
    (void)ok;
  }

  // Non-shared local commit: empty identity, current time as order.
  template <typename ConcreteNode>
  void CommitInto(ConcreteNode& target, Event::ptr event) {
    assert(event.is_valid());
    assert(event.is_loaded());
    assert(event->CanApplyTo(target));

    // Timestamp adjustment, not a logical clock: a Node's own consecutive
    // commits keep the order they were made in even when the wall clock does
    // not advance between them.
    std::uint64_t timestamp_us = SystemUtcMicros();
    if (!journal.empty()) {
      auto const& last = journal.back().order;
      if (timestamp_us <= last.timestamp_us) {
        timestamp_us = last.timestamp_us + 1;
      }
    }

    EventRecord record{
        .event = std::move(event),
        .identity = {},
        .order = SharedEventOrder{.timestamp_us = timestamp_us},
        .retained_since_us = SystemUtcMicros(),
    };
    InsertEvent(target, std::move(record));
  }

  // Shared commit: the Event keeps the identity and timestamp it was given.
  // A remotely originated Event is inserted at its own timestamp; nothing here
  // rewrites it from local state.
  template <typename ConcreteNode>
  bool TryCommitSharedInto(ConcreteNode& target, Event::ptr event,
                           SharedEventId identity, SharedEventOrder order) {
    if (!event.is_valid() || !event.is_loaded() || identity.origin_uid.empty()) {
      return false;
    }

    EventRecord record{
        .event = std::move(event),
        .identity = std::move(identity),
        .order = std::move(order),
        .retained_since_us = SystemUtcMicros(),
    };
    return TryInsertEvent(target, std::move(record));
  }

  template <typename ConcreteNode>
  void CommitSharedInto(ConcreteNode& target, Event::ptr event,
                        SharedEventId identity, SharedEventOrder order) {
    bool const ok = TryCommitSharedInto(target, std::move(event),
                                        std::move(identity), std::move(order));
    assert(ok && "CommitSharedInto invariant broken");
    (void)ok;
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

  virtual bool TryReplayFromBaseImpl() {
    assert(false && "Concrete Node must inherit through NodeFor");
    return false;
  }

  virtual bool TryInsertSharedImpl(Event::ptr event, SharedEventId identity,
                                   SharedEventOrder order) {
    (void)event;
    (void)identity;
    (void)order;
    assert(false && "Concrete Node must inherit through NodeFor");
    return false;
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

// Ordered unique dirty set for GUI publication. Entries are ObjIds, not Node
// pointers: while the GUI holds an unread publication a pending Node may be
// removed from live topology by a later Event, and the graph is then free to
// drop it. The publication step resolves each id against live reachability
// and skips ids that no longer resolve. First-dirty order is preserved;
// membership prevents duplicate entries while Events keep coalescing into one
// pending entry until published.
struct PendingDirtyNodes {
  void Note(Node& node) {
    auto const id = node.obj_id.id();
    if (membership.insert(id).second) {
      ordered.push_back(id);
    }
  }

  [[nodiscard]] bool empty() const { return ordered.empty(); }

  void Clear() {
    ordered.clear();
    membership.clear();
  }

  std::uint32_t PopFront() {
    assert(!ordered.empty());
    std::uint32_t const id = ordered.front();
    ordered.erase(ordered.begin());
    membership.erase(id);
    return id;
  }

  std::vector<std::uint32_t> ordered;
  std::unordered_set<std::uint32_t> membership;
};

inline void PendingDirtyNodesNotify(void* ctx, Node& node) {
  static_cast<PendingDirtyNodes*>(ctx)->Note(node);
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_NODE_H_
