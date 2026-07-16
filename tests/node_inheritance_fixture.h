#ifndef TESTS_NODE_INHERITANCE_FIXTURE_H_
#define TESTS_NODE_INHERITANCE_FIXTURE_H_

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <utility>

#include "aether/obj/obj.h"
#include "aether/obj/obj_ptr.h"

#include "apptraverse/event.h"
#include "apptraverse/event_for.h"
#include "apptraverse/node.h"

namespace apptraverse::test {

inline constexpr ae::ObjId::Type kNode3Id = 1;
inline constexpr ae::ObjId::Type kNode2FactoryId = 2;
inline constexpr ae::ObjId::Type kNode3FactoryId = 3;
inline constexpr ae::ObjId::Type kSetObjectEventPrototypeId = 4;
inline constexpr ae::ObjId::Type kSetValue3EventPrototypeId = 5;
inline constexpr ae::ObjId::Type kNode1AId = 10;
inline constexpr ae::ObjId::Type kNode1BId = 11;
inline constexpr ae::ObjId::Type kSetObjectEventId = 100;
inline constexpr ae::ObjId::Type kSetValue3EventId = 101;
inline constexpr ae::ObjId::Type kBaseSnapshotId = 200;
inline constexpr ae::ObjId::Type kUnusedSecondSnapshotId = 201;

#define APPTRAVERSE_CHECK(cond)                                          \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::cerr << "CHECK failed: " #cond << " (" << __FILE__ << ':'    \
                << __LINE__ << ")\n";                                    \
      std::exit(1);                                                      \
    }                                                                    \
  } while (0)

class Node1;
class Node2;
class Node3;
class Node2Factory;
class Node3Factory;
class SetObjectEvent;
class SetValue3Event;

class Node1 : public apptraverse::Node {
  AE_OBJECT(Node1, apptraverse::Node, 0)

  Node1() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD)
 public:
  Node1(ae::ObjProp prop, std::int32_t marker)
      : Node{prop}, marker_{marker} {}
#endif

 public:
  AE_OBJECT_REFLECT()

  template <typename Dnv>
  void Load(CurrentVersion, Dnv& dnv) {
    dnv(base_);
    if (ShouldTransferBusinessState()) {
      dnv(marker_);
    }
    FinishLoadIfMostDerived<Node1>();
  }

  template <typename Dnv>
  void Save(CurrentVersion, Dnv& dnv) const {
    dnv(base_);
    if (ShouldTransferBusinessState()) {
      dnv(marker_);
    }
  }

  std::int32_t marker() const { return marker_; }

 private:
  std::int32_t marker_{0};
};

class Node2 : public apptraverse::Node {
  AE_OBJECT(Node2, apptraverse::Node, 0)

 protected:
  Node2() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD)
 public:
  Node2(ae::ObjProp prop, ae::ObjId factory_id, Node1::ptr object)
      : Node{prop},
        factory_id_{factory_id},
        object_{std::move(object)} {}
#endif

 public:
  AE_OBJECT_REFLECT()

  template <typename Dnv>
  void Load(CurrentVersion, Dnv& dnv) {
    dnv(base_);
    if (ShouldTransferBusinessState()) {
      dnv(factory_id_, object_);
    }
    FinishLoadIfMostDerived<Node2>();
  }

  template <typename Dnv>
  void Save(CurrentVersion, Dnv& dnv) const {
    dnv(base_);
    if (ShouldTransferBusinessState()) {
      dnv(factory_id_, object_);
    }
  }

  bool SetObject(ae::ObjId snapshot_id, ae::ObjId event_id,
                 ae::ObjId object_id);

  ae::ObjId factory_id() const { return factory_id_; }

  Node1::ptr const& object() {
    object_.Load();
    return object_;
  }

  ae::ObjId object_id() const { return object_.id(); }

  std::uint32_t set_object_apply_calls() const {
    return set_object_apply_calls_;
  }

  ae::ObjId base_snapshot_id_for_test() const { return BaseSnapshotId(); }
  std::size_t journal_size_for_test() const { return JournalSize(); }

  void CorruptObjectForTest(ae::ObjId object_id) {
    if (domain == nullptr || !object_id.IsValid()) {
      return;
    }
    object_ = Node1::ptr::Declare(
        ae::CreateWith{*domain}.with_id(object_id));
  }

 private:
  friend class apptraverse::EventFor<Node2, SetObjectEvent>;

  void Apply(SetObjectEvent const& event);
  ae::ObjPtr<Node2Factory> ResolveFactory();

  ae::ObjId factory_id_;
  Node1::ptr object_;
  std::uint32_t set_object_apply_calls_{0};
};

class Node3 : public Node2 {
  AE_OBJECT(Node3, Node2, 0)

  Node3() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD)
 public:
  Node3(ae::ObjProp prop, ae::ObjId factory_id, Node1::ptr object,
        ae::ObjId factory_id3, std::int32_t value3)
      : Node2{prop, factory_id, std::move(object)},
        factory_id3_{factory_id3},
        value3_{value3} {}
#endif

 public:
  AE_OBJECT_REFLECT()

  template <typename Dnv>
  void Load(CurrentVersion, Dnv& dnv) {
    dnv(base_);
    if (ShouldTransferBusinessState()) {
      dnv(factory_id3_, value3_);
    }
    FinishLoadIfMostDerived<Node3>();
  }

  template <typename Dnv>
  void Save(CurrentVersion, Dnv& dnv) const {
    dnv(base_);
    if (ShouldTransferBusinessState()) {
      dnv(factory_id3_, value3_);
    }
  }

  bool SetValue3(ae::ObjId snapshot_id, ae::ObjId event_id,
                 std::int32_t value);

  ae::ObjId factory_id3() const { return factory_id3_; }
  std::int32_t value3() const { return value3_; }
  std::uint32_t set_value3_apply_calls() const {
    return set_value3_apply_calls_;
  }

  void CorruptValue3ForTest(std::int32_t value) { value3_ = value; }

 private:
  friend class apptraverse::EventFor<Node3, SetValue3Event>;

  void Apply(SetValue3Event const& event);
  ae::ObjPtr<Node3Factory> ResolveFactory3();

  ae::ObjId factory_id3_;
  std::int32_t value3_{30};
  std::uint32_t set_value3_apply_calls_{0};
};

class SetObjectEvent
    : public apptraverse::EventFor<Node2, SetObjectEvent> {
  AE_OBJECT(SetObjectEvent, apptraverse::Event, 0)

  SetObjectEvent() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD)
 public:
  SetObjectEvent(ae::ObjProp prop, ae::ObjId object_id)
      : EventFor{prop}, object_id_{object_id} {}
#endif

 public:
  AE_OBJECT_REFLECT(AE_MMBR(object_id_))

  ae::ObjId object_id() const { return object_id_; }

 private:
  friend class Node2Factory;

  void Initialize(Node2Factory const&, ae::ObjId object_id) {
    object_id_ = object_id;
  }

  ae::ObjId object_id_;
};

class SetValue3Event
    : public apptraverse::EventFor<Node3, SetValue3Event> {
  AE_OBJECT(SetValue3Event, apptraverse::Event, 0)

  SetValue3Event() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD)
 public:
  SetValue3Event(ae::ObjProp prop, std::int32_t value)
      : EventFor{prop}, value_{value} {}
#endif

 public:
  AE_OBJECT_REFLECT(AE_MMBR(value_))

  std::int32_t value() const { return value_; }

 private:
  friend class Node3Factory;

  void Initialize(Node3Factory const&, std::int32_t value) { value_ = value; }

  std::int32_t value_{0};
};

class Node2Factory : public ae::Obj {
  AE_OBJECT(Node2Factory, ae::Obj, 0)

  Node2Factory() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD)
 public:
  Node2Factory(ae::ObjProp prop,
               SetObjectEvent::ptr set_object_event_prototype)
      : ae::Obj{prop},
        set_object_event_prototype_{std::move(set_object_event_prototype)} {}
#endif

 public:
  AE_OBJECT_REFLECT(AE_MMBR(set_object_event_prototype_))

 private:
  friend class Node2;

  SetObjectEvent::ptr CreateSetObjectEvent(ae::ObjId event_id,
                                           ae::ObjId object_id);

  SetObjectEvent::ptr set_object_event_prototype_;
};

class Node3Factory : public ae::Obj {
  AE_OBJECT(Node3Factory, ae::Obj, 0)

  Node3Factory() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD)
 public:
  Node3Factory(ae::ObjProp prop,
               SetValue3Event::ptr set_value3_event_prototype)
      : ae::Obj{prop},
        set_value3_event_prototype_{std::move(set_value3_event_prototype)} {}
#endif

 public:
  AE_OBJECT_REFLECT(AE_MMBR(set_value3_event_prototype_))

 private:
  friend class Node3;

  SetValue3Event::ptr CreateSetValue3Event(ae::ObjId event_id,
                                           std::int32_t value);

  SetValue3Event::ptr set_value3_event_prototype_;
};

inline void Node2::Apply(SetObjectEvent const& event) {
  ++set_object_apply_calls_;
  if (domain == nullptr || !event.object_id().IsValid()) {
    return;
  }
  object_ = Node1::ptr::Declare(
      ae::CreateWith{*domain}.with_id(event.object_id()));
}

inline ae::ObjPtr<Node2Factory> Node2::ResolveFactory() {
  if (domain == nullptr || !factory_id_.IsValid()) {
    return {};
  }

  auto factory = Node2Factory::ptr::Declare(
      ae::CreateWith{*domain}.with_id(factory_id_));
  factory.Load();
  if (!factory) {
    return {};
  }
  return factory;
}

inline bool Node2::SetObject(ae::ObjId snapshot_id, ae::ObjId event_id,
                             ae::ObjId object_id) {
  auto factory = ResolveFactory();
  if (!factory) {
    return false;
  }

  auto event = factory->CreateSetObjectEvent(event_id, object_id);
  if (!event) {
    return false;
  }

  return CommitEvent(event, snapshot_id);
}

inline void Node3::Apply(SetValue3Event const& event) {
  ++set_value3_apply_calls_;
  value3_ = event.value();
}

inline ae::ObjPtr<Node3Factory> Node3::ResolveFactory3() {
  if (domain == nullptr || !factory_id3_.IsValid()) {
    return {};
  }

  auto factory = Node3Factory::ptr::Declare(
      ae::CreateWith{*domain}.with_id(factory_id3_));
  factory.Load();
  if (!factory) {
    return {};
  }
  return factory;
}

inline bool Node3::SetValue3(ae::ObjId snapshot_id, ae::ObjId event_id,
                             std::int32_t value) {
  auto factory = ResolveFactory3();
  if (!factory) {
    return false;
  }

  auto event = factory->CreateSetValue3Event(event_id, value);
  if (!event) {
    return false;
  }

  return CommitEvent(event, snapshot_id);
}

inline SetObjectEvent::ptr Node2Factory::CreateSetObjectEvent(
    ae::ObjId event_id, ae::ObjId object_id) {
  if (!event_id.IsValid() || !set_object_event_prototype_.is_valid()) {
    return {};
  }

  set_object_event_prototype_.Load();
  if (!set_object_event_prototype_) {
    return {};
  }

  auto clone = set_object_event_prototype_.Clone(event_id);
  if (!clone) {
    return {};
  }

  clone->Initialize(*this, object_id);
  return clone;
}

inline SetValue3Event::ptr Node3Factory::CreateSetValue3Event(
    ae::ObjId event_id, std::int32_t value) {
  if (!event_id.IsValid() || !set_value3_event_prototype_.is_valid()) {
    return {};
  }

  set_value3_event_prototype_.Load();
  if (!set_value3_event_prototype_) {
    return {};
  }

  auto clone = set_value3_event_prototype_.Clone(event_id);
  if (!clone) {
    return {};
  }

  clone->Initialize(*this, value);
  return clone;
}

}  // namespace apptraverse::test

#endif  // TESTS_NODE_INHERITANCE_FIXTURE_H_
