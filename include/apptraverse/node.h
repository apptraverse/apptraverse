#ifndef APPTRAVERSE_NODE_H_
#define APPTRAVERSE_NODE_H_

#include <algorithm>
#include <cassert>
#include <utility>
#include <vector>

#include "aether/obj/domain.h"
#include "aether/obj/obj.h"

#include "apptraverse/event_record.h"

namespace apptraverse {

class Node : public ae::Obj {
  AE_OBJECT(Node, Obj, 0)

 protected:
  Node() = default;

 public:
  explicit Node(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(base), AE_MMBR(journal))

  Node::ptr base;
  std::vector<EventRecord> journal;

 protected:
  void ApplyEvent(Event const& event) { event.ApplyTo(*this); }

  void ReplayJournal() {
    for (auto const& record : journal) {
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
    save_graph.Save(target, saved_base.id());

    obj_id = owner_id;
    base = saved_base;
    journal = std::move(saved_journal);

    auto& concrete_base = static_cast<ConcreteNode&>(*base);
    ae::DomainGraph load_graph{domain};
    load_graph.Load(concrete_base, base.id());
  }

  void CommitEvent(Event::ptr event, ae::TimePoint time) {
    assert(domain != nullptr);
    assert(base.is_valid());
    assert(base.is_loaded());
    assert(event.is_valid());
    assert(event.is_loaded());
    assert(event.domain() == domain);
    assert(journal.empty() || journal.back().time < time);

    journal.push_back(EventRecord{
        std::move(event),
        time,
        DeliveryStatus::kPending,
    });

    ApplyEvent(*journal.back().event);
  }

  template <typename ConcreteNode>
  void AcceptRemoteEvent(ConcreteNode& target, Event::ptr event,
                         ae::TimePoint time) {
    assert(domain != nullptr);
    assert(base.is_valid());
    assert(base.is_loaded());
    assert(event.is_valid());
    assert(event.is_loaded());
    assert(event.domain() == domain);

    auto position = std::lower_bound(
        journal.begin(), journal.end(), time,
        [](EventRecord const& record, ae::TimePoint value) {
          return record.time < value;
        });

    assert(position == journal.end() || position->time != time);

    bool const appended = position == journal.end();

    auto inserted = journal.insert(
        position,
        EventRecord{
            std::move(event),
            time,
            DeliveryStatus::kDelivered,
        });

    if (appended) {
      ApplyEvent(*inserted->event);
    } else {
      RebuildFromBaseAndReplay(target);
    }
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_NODE_H_
