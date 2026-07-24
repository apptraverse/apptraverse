#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"

namespace apptraverse::test {

class TypeErasedNode;

class AppendNameEvent
    : public apptraverse::EventFor<TypeErasedNode, AppendNameEvent> {
  AE_OBJECT(AppendNameEvent, Event, 0)

 protected:
  AppendNameEvent() = default;

 public:
  explicit AppendNameEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(suffix))

  std::string suffix;
};

class TypeErasedNode : public apptraverse::NodeFor<TypeErasedNode> {
  AE_OBJECT(TypeErasedNode, Node, 0)

 protected:
  TypeErasedNode() = default;

 public:
  explicit TypeErasedNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name))

  std::string name;

  void Apply(AppendNameEvent const& event) { name += event.suffix; }

  void CaptureBaseStateForTest() { CaptureBaseState(); }

  void CommitEventForTest(apptraverse::Event::ptr event, ae::TimePoint time) {
    CommitEvent(std::move(event), time);
  }
};

}  // namespace apptraverse::test

namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " (" << __FILE__ << ':'         \
                << __LINE__ << ")\n";                                        \
      return EXIT_FAILURE;                                                   \
    }                                                                        \
  } while (0)

}  // namespace

int main() {
  using apptraverse::DeliveryStatus;
  using apptraverse::test::AppendNameEvent;
  using apptraverse::test::TypeErasedNode;

  ae::RamDomainStorage storage;
  ae::Domain domain1{ae::Now(), storage};

  TypeErasedNode::ptr base =
      TypeErasedNode::ptr::Create(ae::CreateWith{domain1}.with_id(1000));
  CHECK(static_cast<bool>(base));
  base->name = "Uninitialized base";
  CHECK(!base->base.is_valid());
  CHECK(base->journal.empty());

  TypeErasedNode::ptr live =
      TypeErasedNode::ptr::Create(ae::CreateWith{domain1}.with_id(100));
  CHECK(static_cast<bool>(live));
  live->name = "Alice";
  live->base = base;
  CHECK(live->journal.empty());

  live->CaptureBaseStateForTest();

  CHECK(live->name == "Alice");
  CHECK(live->base.Load().as<TypeErasedNode>()->name == "Alice");
  CHECK(live->journal.empty());
  CHECK(live->base->journal.empty());

  AppendNameEvent::ptr local_event =
      AppendNameEvent::ptr::Create(ae::CreateWith{domain1}.with_id(200));
  CHECK(static_cast<bool>(local_event));
  local_event->suffix = " Cooper";

  ae::TimePoint const local_time{std::chrono::microseconds{200}};
  live->CommitEventForTest(local_event, local_time);

  CHECK(live->name == "Alice Cooper");
  CHECK(live->journal.size() == 1);
  CHECK(live->journal[0].delivery_status == DeliveryStatus::kPending);

  apptraverse::Node::ptr generic_live = live;
  CHECK(generic_live.is_valid());
  CHECK(generic_live.is_loaded());
  CHECK(generic_live.id().id() == 100);
  CHECK(generic_live.Load().get() == live.Load().get());

  AppendNameEvent::ptr earlier_event =
      AppendNameEvent::ptr::Create(ae::CreateWith{domain1}.with_id(201));
  CHECK(static_cast<bool>(earlier_event));
  earlier_event->suffix = " B.";

  ae::TimePoint const earlier_time{std::chrono::microseconds{100}};
  generic_live->AcceptRemoteEvent(earlier_event, earlier_time);

  CHECK(live->name == "Alice B. Cooper");
  CHECK(live.id().id() == 100);
  CHECK(live->base.id().id() == 1000);
  CHECK(live->journal.size() == 2);

  CHECK(live->journal[0].event.id().id() == 201);
  CHECK(live->journal[0].time == earlier_time);
  CHECK(live->journal[0].delivery_status == DeliveryStatus::kDelivered);

  CHECK(live->journal[1].event.id().id() == 200);
  CHECK(live->journal[1].time == local_time);
  CHECK(live->journal[1].delivery_status == DeliveryStatus::kPending);

  auto* captured_base = live->base.Load().as<TypeErasedNode>();
  CHECK(captured_base != nullptr);
  CHECK(captured_base->name == "Alice");
  CHECK(!captured_base->base.is_valid());
  CHECK(captured_base->journal.empty());

  live.Save();

  ae::Domain domain2{ae::Now(), storage};

  apptraverse::Node::ptr loaded_generic = apptraverse::Node::ptr::Declare(
      ae::CreateWith{domain2}.with_id(100));
  loaded_generic.Load();

  CHECK(loaded_generic.is_valid());
  CHECK(loaded_generic.is_loaded());
  CHECK(loaded_generic.id().id() == 100);
  CHECK(loaded_generic->GetClassId() == TypeErasedNode::kClassId);

  auto* loaded = loaded_generic.Load().as<TypeErasedNode>();
  CHECK(loaded != nullptr);
  CHECK(loaded->name == "Alice B. Cooper");
  CHECK(loaded->base.id().id() == 1000);
  CHECK(loaded->journal.size() == 2);
  CHECK(loaded->journal[0].event.id().id() == 201);
  CHECK(loaded->journal[0].time == earlier_time);
  CHECK(loaded->journal[0].delivery_status == DeliveryStatus::kDelivered);
  CHECK(loaded->journal[1].event.id().id() == 200);
  CHECK(loaded->journal[1].time == local_time);
  CHECK(loaded->journal[1].delivery_status == DeliveryStatus::kPending);

  AppendNameEvent::ptr later_event =
      AppendNameEvent::ptr::Create(ae::CreateWith{domain2}.with_id(202));
  CHECK(static_cast<bool>(later_event));
  later_event->suffix = " Jr.";

  ae::TimePoint const later_time{std::chrono::microseconds{300}};
  loaded_generic->AcceptRemoteEvent(later_event, later_time);

  CHECK(loaded->name == "Alice B. Cooper Jr.");
  CHECK(loaded->name != "Alice B. Cooper Jr. Jr.");
  CHECK(loaded->name != "Alice B. Cooper B. Cooper Jr.");
  CHECK(loaded->journal.size() == 3);
  CHECK(loaded->journal[2].event.id().id() == 202);
  CHECK(loaded->journal[2].time == later_time);
  CHECK(loaded->journal[2].delivery_status == DeliveryStatus::kDelivered);

  auto* loaded_base = loaded->base.Load().as<TypeErasedNode>();
  CHECK(loaded_base != nullptr);
  CHECK(loaded_base->name == "Alice");
  CHECK(!loaded_base->base.is_valid());
  CHECK(loaded_base->journal.empty());

  return EXIT_SUCCESS;
}
