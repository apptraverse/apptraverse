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
#include "apptraverse/node_macros.h"

namespace apptraverse::test {

inline constexpr ae::ObjId::Type kJournalNodeId = 1;
inline constexpr ae::ObjId::Type kJournalFactoryId = 2;
inline constexpr ae::ObjId::Type kJournalEventPrototypeId = 3;
inline constexpr ae::ObjId::Type kFirstEventId = 100;
inline constexpr ae::ObjId::Type kSecondEventId = 102;
inline constexpr ae::ObjId::Type kBaseSnapshotId = 200;
inline constexpr ae::ObjId::Type kUnusedSecondSnapshotId = 201;

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

  std::int32_t value() const { return value_; }
  ae::ObjId factory_id() const { return factory_id_; }
  ae::ObjId base_snapshot_id_for_test() const { return BaseSnapshotId(); }
  std::size_t journal_size_for_test() const { return JournalSize(); }

  void CorruptMaterializedValueForTest(std::int32_t value) { value_ = value; }

 private:
  friend class apptraverse::EventFor<JournalNode, JournalSetValueEvent>;

  void Apply(JournalSetValueEvent const& event);
  ae::ObjPtr<JournalFactory> ResolveFactory();

  ae::ObjId factory_id_;
  std::int32_t value_{10};

  AT_NODE_STATE(factory_id_, value_)
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
