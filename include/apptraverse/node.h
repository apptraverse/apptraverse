#ifndef APPTRAVERSE_NODE_H_
#define APPTRAVERSE_NODE_H_

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <limits>
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
  APPTRAVERSE_OBJECT(Node, ae::Obj, 0)

 protected:
  Node() = default;

 public:
  explicit Node(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(base), AE_MMBR(journal))

  Node::ptr base;
  std::vector<EventRecord> journal;

  // Object-state Generation: the only change signal for materialized state.
  // Not Aether Registry::GenerationDistance (class-hierarchy distance).
  // Increments only when Apply / derived UpdateFromParent actually changes
  // fields. Used to: apply committed Events, refresh before a read, skip
  // re-applying the same journal prefix, and skip UI serialize/deserialize.
  std::uint64_t Generation() const { return generation_; }

  // UI Domain materialization only. Model thread never calls this.
  void AdoptPublishedGeneration(std::uint64_t generation) {
    generation_ = generation;
  }

  virtual void OnLoad() {}

  virtual void Update(std::chrono::steady_clock::time_point now) {
    (void)now;
  }

  // Apply any journal Events not yet materialized. Commit calls this before
  // returning. Call it before reading another object. Re-entry of the same
  // already-applied prefix is a no-op (cursor already advanced).
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

  void NoteMaterializedChange() { ++generation_; }

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
    assert(record.timestamp_us != 0);
    assert(record.event.is_valid());
    assert(record.event.is_loaded());
    assert(record.event.domain() == domain);

    for (auto const& existing : journal) {
      assert(existing.timestamp_us != record.timestamp_us &&
             "duplicate timestamp_us");
    }

    auto position = std::lower_bound(
        journal.begin(), journal.end(), record.timestamp_us,
        [](EventRecord const& existing, std::uint64_t timestamp_us) {
          return existing.timestamp_us < timestamp_us;
        });

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

  template <typename ConcreteNode>
  void CommitInto(ConcreteNode& target, Event::ptr event) {
    assert(event.is_valid());
    assert(event.is_loaded());
    assert(event->CanApplyTo(target));

    std::uint64_t timestamp_us = SystemUtcMicros();
    if (!journal.empty() && timestamp_us <= journal.back().timestamp_us) {
      timestamp_us = journal.back().timestamp_us + 1;
    }

    EventRecord record{
        timestamp_us,
        std::move(event),
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

  std::uint64_t generation_{1};
  std::size_t applied_journal_size_{kJournalFullyMaterialized};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_NODE_H_
