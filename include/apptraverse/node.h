#ifndef APPTRAVERSE_NODE_H_
#define APPTRAVERSE_NODE_H_

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

#include "aether/obj/domain.h"
#include "aether/obj/obj.h"

#include "apptraverse/event_record.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {

inline std::uint64_t SystemUtcMicros() {
  using clock = std::chrono::system_clock;
  auto const now = clock::now().time_since_epoch();
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

  void CaptureBaseStatePublic() { CaptureBaseStateImpl(); }

  void ReloadFromStorage() { ReloadFromStorageImpl(); }

 protected:
  void ApplyEvent(Event const& event) { event.ApplyTo(*this); }

  void ReplayJournal() {
    for (auto const& record : journal) {
      assert(record.event.is_valid());
      assert(record.event.is_loaded());
      ApplyEvent(*record.event);
    }
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
    ReplayJournal();
  }

  template <typename ConcreteNode>
  void CaptureBaseState(ConcreteNode& target) {
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
    auto inserted = journal.insert(position, std::move(record));

    if (appended) {
      ApplyEvent(*inserted->event);
    } else {
      RebuildFromBaseAndReplay(target);
    }
  }

  template <typename ConcreteNode>
  void Commit(ConcreteNode& target, Event::ptr event) {
    assert(event.is_valid());
    assert(event.is_loaded());

    EventRecord record{
        SystemUtcMicros(),
        std::move(event),
    };
    InsertEvent(target, std::move(record));
  }

 private:
  virtual void CaptureBaseStateImpl() {
    assert(false && "Concrete Node must inherit through NodeFor");
  }

  virtual void ReloadFromStorageImpl() {
    assert(false && "Concrete Node must inherit through NodeFor");
  }

  virtual void CommitImpl(Event::ptr event) {
    (void)event;
    assert(false && "Concrete Node must inherit through NodeFor");
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_NODE_H_
