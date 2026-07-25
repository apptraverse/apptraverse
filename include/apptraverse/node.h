#ifndef APPTRAVERSE_NODE_H_
#define APPTRAVERSE_NODE_H_

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
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

  AE_OBJECT_REFLECT(AE_MMBR(base), AE_MMBR(journal),
                    AE_MMBR(next_local_sequence))

  Node::ptr base;
  std::vector<EventRecord> journal;
  std::uint32_t next_local_sequence{1};

  bool AcceptRemoteEvent(Event::ptr event, ae::TimePoint time,
                         EventIdentity identity) {
    return AcceptRemoteEventImpl(std::move(event), time, identity);
  }

  bool ContainsEvent(EventIdentity identity) const {
    assert(identity.IsValid());

    for (auto const& record : journal) {
      if (record.identity == identity) {
        return true;
      }
    }

    return false;
  }

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
    auto saved_next_local_sequence = next_local_sequence;
    ae::DomainGraph graph{domain};
    graph.Load(target, saved_base.id());
    obj_id = owner_id;
    base = saved_base;
    journal = std::move(saved_journal);
    next_local_sequence = saved_next_local_sequence;
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
    auto saved_next_local_sequence = next_local_sequence;

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
    next_local_sequence = saved_next_local_sequence;

    auto& concrete_base = static_cast<ConcreteNode&>(*base);
    ae::DomainGraph load_graph{domain};
    load_graph.Load(concrete_base, base.id());
  }

  void CommitEvent(Event::ptr event, ae::TimePoint time, ae::ObjId origin) {
    CommitEvent(std::move(event), time, origin, {});
  }

  void CommitEvent(Event::ptr event, ae::TimePoint time, ae::ObjId origin,
                   std::vector<ae::ObjId> recipients) {
    assert(domain != nullptr);
    assert(base.is_valid());
    assert(base.is_loaded());
    assert(event.is_valid());
    assert(event.is_loaded());
    assert(event.domain() == domain);
    assert(journal.empty() || journal.back().time < time);
    assert(origin.IsValid());
    assert(next_local_sequence != 0);
    assert(next_local_sequence !=
           std::numeric_limits<std::uint32_t>::max());

    EventIdentity const identity{origin, next_local_sequence};
    ++next_local_sequence;

    assert(identity.IsValid());
    assert(!ContainsEvent(identity));

    for (auto const& recipient : recipients) {
      assert(recipient.IsValid());
    }

    std::sort(recipients.begin(), recipients.end());
    assert(std::adjacent_find(recipients.begin(), recipients.end()) ==
           recipients.end());

    std::vector<EventRecipientState> recipient_states;
    recipient_states.reserve(recipients.size());
    for (auto const& recipient : recipients) {
      recipient_states.push_back(EventRecipientState{
          recipient,
          DeliveryStatus::kPending,
      });
    }

    journal.push_back(EventRecord{
        std::move(event),
        identity,
        time,
        EventRecordOrigin::kLocal,
        std::move(recipient_states),
    });

    ApplyEvent(*journal.back().event);
  }

  template <typename ConcreteNode>
  bool AcceptRemoteEvent(ConcreteNode& target, Event::ptr event,
                         ae::TimePoint time, EventIdentity identity) {
    assert(domain != nullptr);
    assert(base.is_valid());
    assert(base.is_loaded());
    assert(event.is_valid());
    assert(event.is_loaded());
    assert(event.domain() == domain);
    assert(identity.IsValid());

    if (ContainsEvent(identity)) {
      return false;
    }

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
            identity,
            time,
            EventRecordOrigin::kRemote,
            {},
        });

    if (appended) {
      ApplyEvent(*inserted->event);
    } else {
      RebuildFromBaseAndReplay(target);
    }

    return true;
  }

 private:
  virtual bool AcceptRemoteEventImpl(Event::ptr event, ae::TimePoint time,
                                     EventIdentity identity) {
    (void)event;
    (void)time;
    (void)identity;
    assert(false && "Concrete Node must inherit through NodeFor");
    return false;
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_NODE_H_
