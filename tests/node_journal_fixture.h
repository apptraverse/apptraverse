#ifndef TESTS_NODE_JOURNAL_FIXTURE_H_
#define TESTS_NODE_JOURNAL_FIXTURE_H_

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <utility>

#include "aether/obj/obj.h"
#include "aether/obj/obj_ptr.h"

#include "apptraverse/event.h"
#include "apptraverse/event_for.h"
#include "apptraverse/event_record.h"
#include "apptraverse/node_macros.h"

namespace apptraverse::test {

inline constexpr ae::ObjId::Type kJournalNodeId = 1;
inline constexpr ae::ObjId::Type kJournalFactoryId = 2;
inline constexpr ae::ObjId::Type kJournalEventPrototypeId = 3;
inline constexpr ae::ObjId::Type kUnrelatedEventPrototypeId = 4;
inline constexpr ae::ObjId::Type kFirstEventId = 100;
inline constexpr ae::ObjId::Type kSecondEventId = 102;
inline constexpr ae::ObjId::Type kFailedEventId = 103;
inline constexpr ae::ObjId::Type kBaseSnapshotId = 200;
inline constexpr ae::ObjId::Type kUnusedSecondSnapshotId = 201;
inline constexpr ae::ObjId::Type kFailedSnapshotId = 202;

#define APPTRAVERSE_CHECK(cond)                                           \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::cerr << "CHECK failed: " #cond << " (" << __FILE__ << ':'     \
                << __LINE__ << ")\n";                                     \
      std::exit(1);                                                       \
    }                                                                     \
  } while (0)

class JournalNode;
class JournalFactory;
class JournalSetValueEvent;
class UnrelatedNode;
class UnrelatedEvent;

class JournalNode : public apptraverse::Node {
  AT_NODE_OBJECT(JournalNode, apptraverse::Node, 0)

  JournalNode() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD)
 public:
  JournalNode(ae::ObjProp prop, ae::ObjId factory_id,
              std::int32_t initial_value)
      : Node{prop}, factory_id_{factory_id}, value_{initial_value} {}
#endif

 public:
  bool SetValue(ae::ObjId snapshot_id, ae::ObjId event_id,
                std::int32_t value);
  bool TryCommitEventForTest(Event::ptr event, ae::ObjId snapshot_id);

  std::int32_t value() const { return value_; }
  std::uint32_t apply_calls() const { return apply_calls_; }
  ae::ObjId factory_id() const { return factory_id_; }
  ae::ObjId base_snapshot_id_for_test() const { return BaseSnapshotId(); }
  std::size_t journal_size_for_test() const { return JournalSize(); }
  std::uint64_t journal_sequence_at_for_test(std::size_t index) const {
    return JournalSequenceAt(index);
  }
  EventDeliveryState journal_delivery_state_at_for_test(
      std::size_t index) const {
    return JournalDeliveryStateAt(index);
  }
  ae::ObjId journal_event_id_at_for_test(std::size_t index) const {
    return JournalEventIdAt(index);
  }
  Event::ptr journal_event_at_for_test(std::size_t index) const {
    return JournalEventAt(index);
  }

  void CorruptMaterializedValueForTest(std::int32_t value) { value_ = value; }

 private:
  friend class apptraverse::EventFor<JournalNode, JournalSetValueEvent>;

  void Apply(JournalSetValueEvent const& event);
  ae::ObjPtr<JournalFactory> ResolveFactory();

  ae::ObjId factory_id_;
  std::int32_t value_{10};
  std::uint32_t apply_calls_{0};

  AT_NODE_STATE(factory_id_, value_)
};

class UnrelatedNode : public apptraverse::Node {
  AE_OBJECT(UnrelatedNode, apptraverse::Node, 0)

  UnrelatedNode() = default;

 public:
  explicit UnrelatedNode(ae::ObjProp prop) : Node{prop} {}

  AE_OBJECT_REFLECT()

 private:
  friend class apptraverse::EventFor<UnrelatedNode, UnrelatedEvent>;

  void Apply(UnrelatedEvent const&) {}
};

class UnrelatedEvent
    : public apptraverse::EventFor<UnrelatedNode, UnrelatedEvent> {
  AE_OBJECT(UnrelatedEvent, apptraverse::Event, 0)

  UnrelatedEvent() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD)
 public:
  explicit UnrelatedEvent(ae::ObjProp prop) : EventFor{prop} {}
#endif

 public:
  AE_OBJECT_REFLECT()
};

class JournalSetValueEvent
    : public apptraverse::EventFor<JournalNode, JournalSetValueEvent> {
  AE_OBJECT(JournalSetValueEvent, apptraverse::Event, 0)

  JournalSetValueEvent() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD)
 public:
  JournalSetValueEvent(ae::ObjProp prop, std::int32_t value)
      : EventFor{prop}, value_{value} {}
#endif

 public:
  AE_OBJECT_REFLECT(AE_MMBR(value_))

  std::int32_t value() const { return value_; }

 private:
  friend class JournalFactory;

  void Initialize(JournalFactory const&, std::int32_t value) {
    value_ = value;
  }

  std::int32_t value_{0};
};

class JournalFactory : public ae::Obj {
  AE_OBJECT(JournalFactory, ae::Obj, 0)

  JournalFactory() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD)
 public:
  JournalFactory(ae::ObjProp prop,
                 JournalSetValueEvent::ptr set_value_event_prototype)
      : ae::Obj{prop},
        set_value_event_prototype_{std::move(set_value_event_prototype)} {}
#endif

 public:
  AE_OBJECT_REFLECT(AE_MMBR(set_value_event_prototype_))

 private:
  friend class JournalNode;

  JournalSetValueEvent::ptr CreateSetValueEvent(ae::ObjId event_id,
                                                std::int32_t value);

  JournalSetValueEvent::ptr set_value_event_prototype_;
};

inline void JournalNode::Apply(JournalSetValueEvent const& event) {
  value_ = event.value();
  ++apply_calls_;
}

inline bool JournalNode::TryCommitEventForTest(Event::ptr event,
                                               ae::ObjId snapshot_id) {
  return CommitEvent(std::move(event), snapshot_id);
}

inline ae::ObjPtr<JournalFactory> JournalNode::ResolveFactory() {
  if (domain == nullptr || !factory_id_.IsValid()) {
    return {};
  }

  auto factory = JournalFactory::ptr::Declare(
      ae::CreateWith{*domain}.with_id(factory_id_));
  factory.Load();
  if (!factory) {
    return {};
  }
  return factory;
}

inline bool JournalNode::SetValue(ae::ObjId snapshot_id, ae::ObjId event_id,
                                  std::int32_t value) {
  auto factory = ResolveFactory();
  if (!factory) {
    return false;
  }

  auto event = factory->CreateSetValueEvent(event_id, value);
  if (!event) {
    return false;
  }

  return CommitEvent(event, snapshot_id);
}

inline JournalSetValueEvent::ptr JournalFactory::CreateSetValueEvent(
    ae::ObjId event_id, std::int32_t value) {
  if (!event_id.IsValid() || !set_value_event_prototype_.is_valid()) {
    return {};
  }

  set_value_event_prototype_.Load();
  if (!set_value_event_prototype_) {
    return {};
  }

  auto clone = set_value_event_prototype_.Clone(event_id);
  if (!clone) {
    return {};
  }

  clone->Initialize(*this, value);
  return clone;
}

}  // namespace apptraverse::test

#endif  // TESTS_NODE_JOURNAL_FIXTURE_H_
