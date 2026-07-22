#ifndef TESTS_EVENT_STATE_VERSION_FIXTURE_H_
#define TESTS_EVENT_STATE_VERSION_FIXTURE_H_

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <utility>

#include "aether/obj/obj.h"
#include "aether/obj/obj_ptr.h"

#include "apptraverse/event.h"
#include "apptraverse/event_for.h"
#include "apptraverse/node_macros.h"
#include "apptraverse/replica_id.h"

namespace apptraverse::test {

inline constexpr ae::ObjId::Type kEventVersionNodeId = 1;
inline constexpr ae::ObjId::Type kEventVersionFactoryId = 2;
inline constexpr ae::ObjId::Type kAddValueEventPrototypeId = 3;
inline constexpr ae::ObjId::Type kOldEventId = 100;
inline constexpr ae::ObjId::Type kNewEventId = 101;
inline constexpr ae::ObjId::Type kBaseSnapshotId = 200;
inline constexpr ae::ObjId::Type kUnusedSnapshotId = 201;

#if defined(APPTRAVERSE_EVENT_SCHEMA_V0)
inline constexpr std::uint32_t kEventSchemaVersion = 0;
#else
inline constexpr std::uint32_t kEventSchemaVersion = 1;
#endif

#define APPTRAVERSE_CHECK(cond)                                          \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::cerr << "CHECK failed: " #cond << " (" << __FILE__ << ':'    \
                << __LINE__ << ")\n";                                    \
      std::exit(1);                                                      \
    }                                                                    \
  } while (0)

class EventVersionNode;
class EventVersionFactory;
class AddValueEvent;

class EventVersionNode : public apptraverse::Node {
  AT_NODE_OBJECT(EventVersionNode, apptraverse::Node, 0)

  EventVersionNode() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD)
 public:
  EventVersionNode(ae::ObjProp prop, ae::ObjId factory_id,
                   std::int64_t initial_value)
      : Node{prop}, factory_id_{factory_id}, value_{initial_value} {}
#endif

 public:
  bool AddValue(ae::ObjId snapshot_id, ae::ObjId event_id,
                std::int64_t logical_delta);

  ae::ObjId factory_id() const { return factory_id_; }
  std::int64_t value_for_test() const { return value_; }
  std::uint32_t add_value_apply_calls_for_test() const {
    return add_value_apply_calls_;
  }
  ae::ObjId base_snapshot_id_for_test() const { return BaseSnapshotId(); }
  std::size_t journal_size_for_test() const { return JournalSize(); }

  void InitializeReplicaForTest(apptraverse::ReplicaId replica_id) {
    InitializeReplica(replica_id);
  }

  void CorruptValueForTest(std::int64_t value) { value_ = value; }

 private:
  friend class apptraverse::EventFor<EventVersionNode, AddValueEvent>;

  void Apply(AddValueEvent const& event);
  ae::ObjPtr<EventVersionFactory> ResolveFactory();

  ae::ObjId factory_id_;
  std::int64_t value_{100};
  std::uint32_t add_value_apply_calls_{0};

  AT_NODE_STATE(factory_id_, value_)
};

class AddValueEvent
    : public apptraverse::EventFor<EventVersionNode, AddValueEvent> {
  AE_OBJECT(AddValueEvent, apptraverse::Event, kEventSchemaVersion)

  AddValueEvent() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD)
 public:
  AddValueEvent(ae::ObjProp prop, std::int64_t logical_delta)
      : EventFor{prop},
        legacy_delta_{static_cast<std::int32_t>(logical_delta / 10)} {}
#endif

 public:
  AE_OBJECT_REFLECT()

#if defined(APPTRAVERSE_EVENT_SCHEMA_V0)
  template <typename Dnv>
  void Load(CurrentVersion, Dnv& dnv) {
    dnv(base_, legacy_delta_);
  }

  template <typename Dnv>
  void Save(CurrentVersion, Dnv& dnv) const {
    dnv(base_, legacy_delta_);
  }

  std::int32_t legacy_delta_for_test() const { return legacy_delta_; }

  std::int64_t logical_delta() const {
    return static_cast<std::int64_t>(legacy_delta_) * 10;
  }
#else
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_);
    std::int32_t legacy_delta = 0;
    dnv(legacy_delta);
    delta_ = static_cast<std::int64_t>(legacy_delta) * 10;
    mode_ = 1;
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_);
    auto legacy_delta = static_cast<std::int32_t>(delta_ / 10);
    dnv(legacy_delta);
  }

  template <typename Dnv>
  void Load(CurrentVersion, Dnv& dnv) {
    dnv(delta_, mode_);
  }

  template <typename Dnv>
  void Save(CurrentVersion, Dnv& dnv) const {
    dnv(delta_, mode_);
  }

  std::int64_t delta_for_test() const { return delta_; }
  std::uint32_t mode_for_test() const { return mode_; }

  std::int64_t logical_delta() const { return delta_; }
#endif

 private:
  friend class EventVersionFactory;

  void Initialize(EventVersionFactory const&, std::int64_t logical_delta);

#if defined(APPTRAVERSE_EVENT_SCHEMA_V0)
  std::int32_t legacy_delta_{0};
#else
  std::int64_t delta_{0};
  std::uint32_t mode_{0};
#endif
};

class EventVersionFactory : public ae::Obj {
  AE_OBJECT(EventVersionFactory, ae::Obj, 0)

  EventVersionFactory() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD)
 public:
  EventVersionFactory(ae::ObjProp prop,
                      AddValueEvent::ptr add_value_event_prototype)
      : ae::Obj{prop},
        add_value_event_prototype_{std::move(add_value_event_prototype)} {}
#endif

 public:
  AE_OBJECT_REFLECT(AE_MMBR(add_value_event_prototype_))

 private:
  friend class EventVersionNode;

  AddValueEvent::ptr CreateAddValueEvent(ae::ObjId event_id,
                                         std::int64_t logical_delta);

  AddValueEvent::ptr add_value_event_prototype_;
};

inline void AddValueEvent::Initialize(EventVersionFactory const&,
                                      std::int64_t logical_delta) {
#if defined(APPTRAVERSE_EVENT_SCHEMA_V0)
  legacy_delta_ = static_cast<std::int32_t>(logical_delta / 10);
#else
  delta_ = logical_delta;
  mode_ = 7;
#endif
}

inline void EventVersionNode::Apply(AddValueEvent const& event) {
  ++add_value_apply_calls_;
  value_ += event.logical_delta();
}

inline ae::ObjPtr<EventVersionFactory> EventVersionNode::ResolveFactory() {
  if (domain == nullptr || !factory_id_.IsValid()) {
    return {};
  }

  auto factory = EventVersionFactory::ptr::Declare(
      ae::CreateWith{*domain}.with_id(factory_id_));
  factory.Load();
  if (!factory) {
    return {};
  }
  return factory;
}

inline bool EventVersionNode::AddValue(ae::ObjId snapshot_id,
                                       ae::ObjId event_id,
                                       std::int64_t logical_delta) {
  auto factory = ResolveFactory();
  if (!factory) {
    return false;
  }

  auto event = factory->CreateAddValueEvent(event_id, logical_delta);
  if (!event) {
    return false;
  }

  CommitEvent(event, snapshot_id);
  return true;
}

inline AddValueEvent::ptr EventVersionFactory::CreateAddValueEvent(
    ae::ObjId event_id, std::int64_t logical_delta) {
  if (!event_id.IsValid() || !add_value_event_prototype_.is_valid()) {
    return {};
  }

  add_value_event_prototype_.Load();
  if (!add_value_event_prototype_) {
    return {};
  }

  auto clone = add_value_event_prototype_.Clone(event_id);
  if (!clone) {
    return {};
  }

  clone->Initialize(*this, logical_delta);
  return clone;
}

}  // namespace apptraverse::test

#endif  // TESTS_EVENT_STATE_VERSION_FIXTURE_H_
