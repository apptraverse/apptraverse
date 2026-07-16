#ifndef TESTS_TYPE_OWNED_FACTORY_FIXTURE_H_
#define TESTS_TYPE_OWNED_FACTORY_FIXTURE_H_

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>
#include <utility>

#include "aether/obj/obj.h"
#include "aether/obj/obj_ptr.h"

#include "apptraverse/event.h"
#include "apptraverse/event_for.h"
#include "apptraverse/node.h"

namespace apptraverse::test {

inline constexpr ae::ObjId::Type kHandlerNodeId = 1;
inline constexpr ae::ObjId::Type kHandlerFactoryId = 2;
inline constexpr ae::ObjId::Type kSetValueEventPrototypeId = 3;
inline constexpr ae::ObjId::Type kRuntimeObjectPrototypeId = 4;
inline constexpr ae::ObjId::Type kRuntimeEventId = 100;
inline constexpr ae::ObjId::Type kRuntimeObjectId = 101;
inline constexpr ae::ObjId::Type kSecondRuntimeEventId = 102;

#define APPTRAVERSE_CHECK(cond)                                           \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::cerr << "CHECK failed: " #cond << " (" << __FILE__ << ':'     \
                << __LINE__ << ")\n";                                     \
      std::exit(1);                                                       \
    }                                                                     \
  } while (0)

class HandlerNode;
class HandlerFactory;
class SetValueEvent;

class RuntimeObject : public ae::Obj {
  AE_OBJECT(RuntimeObject, ae::Obj, 0)

  RuntimeObject() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD)
 public:
  RuntimeObject(ae::ObjProp prop, std::int32_t prototype_marker)
      : ae::Obj{prop}, prototype_marker_{prototype_marker} {}
#endif

 public:
  AE_OBJECT_REFLECT(AE_MMBR(prototype_marker_), AE_MMBR(runtime_value_))

  std::int32_t prototype_marker() const { return prototype_marker_; }
  std::int32_t runtime_value() const { return runtime_value_; }

 private:
  friend class HandlerFactory;

  void Initialize(HandlerFactory const&, std::int32_t runtime_value) {
    runtime_value_ = runtime_value;
  }

  std::int32_t prototype_marker_{0};
  std::int32_t runtime_value_{0};
};

class HandlerNode : public apptraverse::Node {
  AE_OBJECT(HandlerNode, apptraverse::Node, 0)

  HandlerNode() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD)
 public:
  HandlerNode(ae::ObjProp prop, ae::ObjId factory_id,
              std::int32_t initial_value)
      : Node{prop}, factory_id_{factory_id}, value_{initial_value} {}
#endif

 public:
  AE_OBJECT_REFLECT(AE_MMBR(factory_id_), AE_MMBR(value_), AE_MMBR(last_event_),
                    AE_MMBR(runtime_object_))

  bool SetValue(ae::ObjId event_id, std::int32_t value);
  bool CreateRuntimeObject(ae::ObjId object_id, std::int32_t runtime_value);

  std::int32_t value() const { return value_; }
  ae::ObjId factory_id() const { return factory_id_; }
  ae::ObjPtr<SetValueEvent> const& last_event() const { return last_event_; }
  RuntimeObject::ptr const& runtime_object() const { return runtime_object_; }

 private:
  friend class apptraverse::EventFor<HandlerNode, SetValueEvent>;

  void Apply(SetValueEvent const& event);
  ae::ObjPtr<HandlerFactory> ResolveFactory();

  ae::ObjId factory_id_;
  std::int32_t value_{10};
  ae::ObjPtr<SetValueEvent> last_event_;
  RuntimeObject::ptr runtime_object_;
};

class SetValueEvent
    : public apptraverse::EventFor<HandlerNode, SetValueEvent> {
  AE_OBJECT(SetValueEvent, apptraverse::Event, 0)

  SetValueEvent() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD)
 public:
  SetValueEvent(ae::ObjProp prop, std::int32_t value)
      : EventFor{prop}, value_{value} {}
#endif

 public:
  AE_OBJECT_REFLECT(AE_MMBR(value_))

  std::int32_t value() const { return value_; }

 private:
  friend class HandlerFactory;

  void Initialize(HandlerFactory const&, std::int32_t value) { value_ = value; }

  std::int32_t value_{0};
};

class HandlerFactory : public ae::Obj {
  AE_OBJECT(HandlerFactory, ae::Obj, 0)

  HandlerFactory() = default;

#if defined(APPTRAVERSE_DISTILLATION_BUILD)
 public:
  HandlerFactory(ae::ObjProp prop, SetValueEvent::ptr set_value_event_prototype,
                 RuntimeObject::ptr runtime_object_prototype)
      : ae::Obj{prop},
        set_value_event_prototype_{std::move(set_value_event_prototype)},
        runtime_object_prototype_{std::move(runtime_object_prototype)} {}
#endif

 public:
  AE_OBJECT_REFLECT(AE_MMBR(set_value_event_prototype_),
                    AE_MMBR(runtime_object_prototype_))

 private:
  friend class HandlerNode;

  SetValueEvent::ptr CreateSetValueEvent(ae::ObjId event_id,
                                         std::int32_t value);
  RuntimeObject::ptr CreateRuntimeObject(ae::ObjId object_id,
                                         std::int32_t runtime_value);

  SetValueEvent::ptr set_value_event_prototype_;
  RuntimeObject::ptr runtime_object_prototype_;
};

inline void HandlerNode::Apply(SetValueEvent const& event) {
  value_ = event.value();
}

inline ae::ObjPtr<HandlerFactory> HandlerNode::ResolveFactory() {
  if (domain == nullptr || !factory_id_.IsValid()) {
    return {};
  }

  auto factory =
      HandlerFactory::ptr::Declare(ae::CreateWith{*domain}.with_id(factory_id_));
  factory.Load();
  if (!factory) {
    return {};
  }
  return factory;
}

inline bool HandlerNode::SetValue(ae::ObjId event_id, std::int32_t value) {
  auto factory = ResolveFactory();
  if (!factory) {
    return false;
  }

  auto event = factory->CreateSetValueEvent(event_id, value);
  if (!event) {
    return false;
  }

  if (!ApplyEvent(*event)) {
    return false;
  }

  last_event_ = event;
  return true;
}

inline bool HandlerNode::CreateRuntimeObject(ae::ObjId object_id,
                                             std::int32_t runtime_value) {
  auto factory = ResolveFactory();
  if (!factory) {
    return false;
  }

  auto object = factory->CreateRuntimeObject(object_id, runtime_value);
  if (!object) {
    return false;
  }

  runtime_object_ = object;
  return true;
}

inline SetValueEvent::ptr HandlerFactory::CreateSetValueEvent(
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

inline RuntimeObject::ptr HandlerFactory::CreateRuntimeObject(
    ae::ObjId object_id, std::int32_t runtime_value) {
  if (!object_id.IsValid() || !runtime_object_prototype_.is_valid()) {
    return {};
  }

  runtime_object_prototype_.Load();
  if (!runtime_object_prototype_) {
    return {};
  }

  auto clone = runtime_object_prototype_.Clone(object_id);
  if (!clone) {
    return {};
  }

  clone->Initialize(*this, runtime_value);
  return clone;
}

}  // namespace apptraverse::test

#endif  // TESTS_TYPE_OWNED_FACTORY_FIXTURE_H_
