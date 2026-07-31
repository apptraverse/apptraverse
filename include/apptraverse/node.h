#ifndef APPTRAVERSE_NODE_H_
#define APPTRAVERSE_NODE_H_

#include <algorithm>
#include <cassert>
#include <utility>
#include <vector>

#include "aether/obj/domain.h"
#include "aether/obj/obj.h"

#include "apptraverse/event_identity.h"
#include "apptraverse/event_order.h"
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

  bool ContainsEvent(EventIdentity const& identity) const {
    assert(identity.IsValid());
    for (auto const& record : journal) {
      if (record.identity == identity) {
        return true;
      }
    }
    return false;
  }

  EventRecord const* FindRecord(EventIdentity const& identity) const {
    for (auto const& record : journal) {
      if (record.identity == identity) {
        return &record;
      }
    }
    return nullptr;
  }

  bool AcceptSharedEvent(EventRecord record) {
    return AcceptSharedEventImpl(std::move(record));
  }

  void CaptureBaseStatePublic() { CaptureBaseStateImpl(); }

  void CollapseSharedPrefix(std::size_t prefix_count) {
    CollapseSharedPrefixImpl(prefix_count);
  }

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
  bool InsertSharedEvent(ConcreteNode& target, EventRecord record) {
    assert(domain != nullptr);
    assert(base.is_valid());
    assert(base.is_loaded());
    assert(record.identity.IsValid());
    assert(record.order.IsValid());
    assert(record.event.is_valid());
    assert(record.event.is_loaded());
    assert(record.event.domain() == domain);

    if (ContainsEvent(record.identity)) {
      return false;
    }

    auto position = std::lower_bound(
        journal.begin(), journal.end(), record.order,
        [](EventRecord const& existing, EventOrder const& order) {
          return existing.order < order;
        });

    assert(position == journal.end() || !(position->order == record.order));

    bool const appended = position == journal.end();
    auto inserted = journal.insert(position, std::move(record));

    if (appended) {
      ApplyEvent(*inserted->event);
    } else {
      RebuildFromBaseAndReplay(target);
    }

    return true;
  }

  template <typename ConcreteNode>
  void CollapsePrefix(ConcreteNode& target, std::size_t prefix_count) {
    assert(prefix_count <= journal.size());
    if (prefix_count == 0) {
      return;
    }

    auto remaining = std::vector<EventRecord>(
        journal.begin() + static_cast<std::ptrdiff_t>(prefix_count),
        journal.end());

    // Materialize old_base + prefix, capture that as the new base, then re-apply
    // the unconfirmed suffix so the current state stays unchanged.
    journal.erase(journal.begin() + static_cast<std::ptrdiff_t>(prefix_count),
                  journal.end());
    RebuildFromBaseAndReplay(target);
    journal.clear();
    CaptureBaseState(target);
    journal = std::move(remaining);
    for (auto const& record : journal) {
      assert(record.event.is_valid());
      assert(record.event.is_loaded());
      ApplyEvent(*record.event);
    }
  }

 private:
  virtual bool AcceptSharedEventImpl(EventRecord record) {
    (void)record;
    assert(false && "Concrete Node must inherit through NodeFor");
    return false;
  }

  virtual void CaptureBaseStateImpl() {
    assert(false && "Concrete Node must inherit through NodeFor");
  }

  virtual void CollapseSharedPrefixImpl(std::size_t prefix_count) {
    (void)prefix_count;
    assert(false && "Concrete Node must inherit through NodeFor");
  }

  virtual void ReloadFromStorageImpl() {
    assert(false && "Concrete Node must inherit through NodeFor");
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_NODE_H_
