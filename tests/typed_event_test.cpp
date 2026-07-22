#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>

#include "aether/obj/domain.h"
#include "aether/domain_storage/ram_domain_storage.h"

#include "apptraverse/event.h"
#include "apptraverse/event_for.h"
#include "apptraverse/node.h"

namespace at = apptraverse;

namespace {

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::cerr << "CHECK failed: " #cond << " (" << __FILE__ << ':'     \
                << __LINE__ << ")\n";                                     \
      std::exit(1);                                                       \
    }                                                                     \
  } while (0)

class SetValueEvent;

class Node1 : public at::Node {
  AE_OBJECT(Node1, at::Node, 0)

 protected:
  Node1() = default;

 public:
  explicit Node1(ae::ObjProp prop) : Node{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value_), AE_MMBR(apply_calls_))

  std::int32_t value() const { return value_; }
  std::uint32_t apply_calls() const { return apply_calls_; }

  void ApplyForTest(at::Event const& event) { ApplyEvent(event); }

 private:
  friend class at::EventFor<Node1, SetValueEvent>;

  void Apply(SetValueEvent const& event);

  std::int32_t value_{0};
  std::uint32_t apply_calls_{0};
};

class Node1Derived : public Node1 {
  AE_OBJECT(Node1Derived, Node1, 0)

  Node1Derived() = default;

 public:
  explicit Node1Derived(ae::ObjProp prop) : Node1{prop} {}

  AE_OBJECT_REFLECT()
};

class SetValueEvent : public at::EventFor<Node1, SetValueEvent> {
  AE_OBJECT(SetValueEvent, at::Event, 0)

  SetValueEvent() = default;

 public:
  SetValueEvent(ae::ObjProp prop, std::int32_t value)
      : EventFor{prop}, value_{value} {}

  AE_OBJECT_REFLECT(AE_MMBR(value_))

  std::int32_t value() const { return value_; }

 private:
  std::int32_t value_{0};
};

void Node1::Apply(SetValueEvent const& event) {
  value_ = event.value();
  ++apply_calls_;
}

static_assert(std::is_base_of_v<ae::Obj, at::Node>);
static_assert(std::is_base_of_v<ae::Obj, at::Event>);
static_assert(std::is_base_of_v<at::Node, Node1>);
static_assert(std::is_base_of_v<Node1, Node1Derived>);
static_assert(std::is_base_of_v<at::Event, SetValueEvent>);
static_assert(SetValueEvent::kBaseClassId == at::Event::kClassId);

template <typename EventT>
concept ExternalCanCallApplyTo = requires(EventT const& event, at::Node& node) {
  { event.ApplyTo(node) } -> std::same_as<void>;
};

template <typename EventT>
constexpr bool ExternalApplyToIsPrivate() {
  if constexpr (ExternalCanCallApplyTo<EventT>) {
    return false;
  }
  return true;
}

static_assert(ExternalApplyToIsPrivate<at::Event>());

}  // namespace

int main() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};

  auto event = SetValueEvent::ptr::Create(ae::CreateWith{domain}.with_id(100),
                                          std::int32_t{42});
  CHECK(event);
  CHECK(event->value() == 42);

  at::Event::ptr as_event = event;
  CHECK(as_event);

  {
    auto node =
        Node1::ptr::Create(ae::CreateWith{domain}.with_id(1));
    CHECK(node);
    CHECK(node->value() == 0);
    CHECK(node->apply_calls() == 0);

    node->ApplyForTest(*event);
    CHECK(node->value() == 42);
    CHECK(node->apply_calls() == 1);

    node->ApplyForTest(*event);
    CHECK(node->value() == 42);
    CHECK(node->apply_calls() == 2);
  }

  {
    auto node =
        Node1Derived::ptr::Create(ae::CreateWith{domain}.with_id(2));
    CHECK(node);
    CHECK(node->value() == 0);
    CHECK(node->apply_calls() == 0);

    node->ApplyForTest(*event);
    CHECK(node->value() == 42);
    CHECK(node->apply_calls() == 1);
  }

  return 0;
}
