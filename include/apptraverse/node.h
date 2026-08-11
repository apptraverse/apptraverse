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

namespace detail {
struct SharedDiscoveryContext;
struct OwnedObjectIdCollector;
}

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

  void CaptureBaseState() { CaptureBaseStateImpl(); }

  void Commit(Event::ptr event) { CommitImpl(std::move(event)); }

  void ReloadFromStorage() { ReloadFromStorageImpl(); }

  // Deep-reflect concrete Node state for shared-graph discovery.
  void ReflectForSharedDiscovery(detail::SharedDiscoveryContext& ctx) {
    ReflectForSharedDiscoveryImpl(ctx);
  }

  // Mask Local/Shared edges and collect ordinary owned ObjIds for transfer.
  void PrepareScopedTransfer(detail::OwnedObjectIdCollector& owned) {
    PrepareScopedTransferImpl(owned);
  }

  bool HasEvent(ae::ObjId event_id) const {
    for (auto const& record : journal) {
      if (record.event.id() == event_id) {
        return true;
      }
    }
    return false;
  }

  // Insert a remote Event with its original timestamp. Returns false if the
  // Event ObjId is already present (duplicate).
  bool AcceptRemoteEvent(Event::ptr event,
                         std::uint64_t original_timestamp_us) {
    return AcceptRemoteEventImpl(std::move(event), original_timestamp_us);
  }

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
    auto inserted = journal.insert(position, std::move(record));

    if (appended) {
      ApplyEvent(*inserted->event);
    } else {
      RebuildFromBaseAndReplay(target);
    }
  }

  template <typename ConcreteNode>
  void CommitInto(ConcreteNode& target, Event::ptr event) {
    assert(event.is_valid());
    assert(event.is_loaded());

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

  template <typename ConcreteNode>
  bool AcceptRemoteEventInto(ConcreteNode& target, Event::ptr event,
                             std::uint64_t original_timestamp_us) {
    assert(event.is_valid());
    assert(event.is_loaded());
    assert(original_timestamp_us != 0);
    if (HasEvent(event.id())) {
      return false;
    }
    EventRecord record{
        original_timestamp_us,
        std::move(event),
    };
    InsertEvent(target, std::move(record));
    return true;
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

  virtual void ReflectForSharedDiscoveryImpl(
      detail::SharedDiscoveryContext& ctx) {
    (void)ctx;
    assert(false && "Concrete Node must inherit through NodeFor");
  }

  virtual void PrepareScopedTransferImpl(
      detail::OwnedObjectIdCollector& owned) {
    (void)owned;
    assert(false && "Concrete Node must inherit through NodeFor");
  }

  virtual bool AcceptRemoteEventImpl(Event::ptr event,
                                     std::uint64_t original_timestamp_us) {
    (void)event;
    (void)original_timestamp_us;
    assert(false && "Concrete Node must inherit through NodeFor");
    return false;
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_NODE_H_
