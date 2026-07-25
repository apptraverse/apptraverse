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

class LeafNode;

class AppendLeafNodeNameEvent
    : public apptraverse::EventFor<LeafNode, AppendLeafNodeNameEvent> {
  AE_OBJECT(AppendLeafNodeNameEvent, Event, 0)

 protected:
  AppendLeafNodeNameEvent() = default;

 public:
  explicit AppendLeafNodeNameEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(suffix))

  std::string suffix;
};

class LeafNode : public apptraverse::NodeFor<LeafNode> {
  AE_OBJECT(LeafNode, Node, 0)

 protected:
  LeafNode() = default;

 public:
  explicit LeafNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name))

  std::string name;

  void Apply(AppendLeafNodeNameEvent const& event) { name += event.suffix; }

  void CaptureBaseStateForTest() { CaptureBaseState(); }

  void RebuildFromBaseAndReplayForTest() { RebuildFromBaseAndReplay(); }

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
  using apptraverse::test::AppendLeafNodeNameEvent;
  using apptraverse::test::LeafNode;

  ae::RamDomainStorage storage;

  ae::Domain domain1{ae::Now(), storage};

  LeafNode::ptr base =
      LeafNode::ptr::Create(ae::CreateWith{domain1}.with_id(1000));
  CHECK(static_cast<bool>(base));
  base->name = "Old base";
  CHECK(!base->base.is_valid());
  CHECK(base->journal.empty());

  LeafNode::ptr live =
      LeafNode::ptr::Create(ae::CreateWith{domain1}.with_id(100));
  CHECK(static_cast<bool>(live));
  live->name = "Root";
  live->base = base;
  CHECK(live->journal.empty());
  CHECK(live->base.is_valid());
  CHECK(live->base.is_loaded());

  live->CaptureBaseStateForTest();

  CHECK(live.id().id() == 100);
  CHECK(live->name == "Root");
  CHECK(live->base.id().id() == 1000);
  CHECK(live->journal.empty());

  auto* captured_base = live->base.Load().as<LeafNode>();
  CHECK(captured_base != nullptr);
  CHECK(captured_base->name == "Root");
  CHECK(!captured_base->base.is_valid());
  CHECK(captured_base->journal.empty());

  AppendLeafNodeNameEvent::ptr first_event =
      AppendLeafNodeNameEvent::ptr::Create(ae::CreateWith{domain1}.with_id(200));
  CHECK(static_cast<bool>(first_event));
  first_event->suffix = " B.";

  ae::TimePoint const first_time{std::chrono::microseconds{100}};
  first_event->sender = live;
  live->CommitEventForTest(first_event, first_time);

  CHECK(live->name == "Root B.");
  CHECK(live.id().id() == 100);
  CHECK(live->base.id().id() == 1000);
  CHECK(live->journal.size() == 1);
  CHECK(live->journal[0].event.id().id() == 200);
  CHECK(live->journal[0].event->sender.id() == live.id());
  CHECK(!live->journal[0].event->sender.is_loaded());
  CHECK(live->journal[0].event->sequence == 1);
  CHECK(live->journal[0].time == first_time);
  CHECK(live->journal[0].recipients.empty());
  CHECK(live->next_local_sequence == 2);

  auto* base_after_first = live->base.Load().as<LeafNode>();
  CHECK(base_after_first != nullptr);
  CHECK(base_after_first->name == "Root");
  CHECK(base_after_first->journal.empty());

  AppendLeafNodeNameEvent::ptr second_event =
      AppendLeafNodeNameEvent::ptr::Create(ae::CreateWith{domain1}.with_id(201));
  CHECK(static_cast<bool>(second_event));
  second_event->suffix = " Value";

  ae::TimePoint const second_time{std::chrono::microseconds{200}};
  second_event->sender = live;
  live->CommitEventForTest(second_event, second_time);

  CHECK(live->name == "Root B. Value");
  CHECK(live->name != "Root B. B. Value");
  CHECK(live->journal.size() == 2);
  CHECK(live->journal[0].event.id().id() == 200);
  CHECK(live->journal[1].event.id().id() == 201);
  CHECK(live->journal[0].event->sender.id() == live.id());
  CHECK(live->journal[1].event->sender.id() == live.id());
  CHECK(live->journal[0].event->sequence == 1);
  CHECK(live->journal[1].event->sequence == 2);
  CHECK(live->journal[0].time == first_time);
  CHECK(live->journal[1].time == second_time);
  CHECK(live->journal[0].recipients.empty());
  CHECK(live->journal[1].recipients.empty());
  CHECK(live->next_local_sequence == 3);

  auto* base_after_second = live->base.Load().as<LeafNode>();
  CHECK(base_after_second != nullptr);
  CHECK(base_after_second->name == "Root");
  CHECK(!base_after_second->base.is_valid());
  CHECK(base_after_second->journal.empty());

  live.Save();

  ae::Domain domain2{ae::Now(), storage};
  LeafNode::ptr loaded =
      LeafNode::ptr::Declare(ae::CreateWith{domain2}.with_id(100));
  loaded.Load();

  CHECK(static_cast<bool>(loaded));
  CHECK(loaded.id().id() == 100);
  CHECK(loaded->name == "Root B. Value");
  CHECK(loaded->base.id().id() == 1000);
  CHECK(loaded->journal.size() == 2);

  CHECK(loaded->journal[0].event.id().id() == 200);
  CHECK(loaded->journal[0].time == first_time);
  CHECK(loaded->journal[0].recipients.empty());
  CHECK(loaded->journal[0].event.is_valid());
  CHECK(loaded->journal[0].event.is_loaded());
  CHECK(loaded->journal[0].event->GetClassId() ==
        AppendLeafNodeNameEvent::kClassId);
  auto* loaded_first =
      loaded->journal[0].event.Load().as<AppendLeafNodeNameEvent>();
  CHECK(loaded_first != nullptr);
  CHECK(loaded_first->suffix == " B.");

  CHECK(loaded->journal[1].event.id().id() == 201);
  CHECK(loaded->journal[1].time == second_time);
  CHECK(loaded->journal[1].recipients.empty());
  CHECK(loaded->journal[1].event.is_valid());
  CHECK(loaded->journal[1].event.is_loaded());
  CHECK(loaded->journal[1].event->GetClassId() ==
        AppendLeafNodeNameEvent::kClassId);
  auto* loaded_second =
      loaded->journal[1].event.Load().as<AppendLeafNodeNameEvent>();
  CHECK(loaded_second != nullptr);
  CHECK(loaded_second->suffix == " Value");

  auto* loaded_base = loaded->base.Load().as<LeafNode>();
  CHECK(loaded_base != nullptr);
  CHECK(loaded_base->name == "Root");
  CHECK(!loaded_base->base.is_valid());
  CHECK(loaded_base->journal.empty());

  loaded->name = "Transient value";
  loaded->RebuildFromBaseAndReplayForTest();

  CHECK(loaded->name == "Root B. Value");
  CHECK(loaded.id().id() == 100);
  CHECK(loaded->base.id().id() == 1000);
  CHECK(loaded->next_local_sequence == 3);
  CHECK(loaded->journal.size() == 2);
  CHECK(loaded->journal[0].event.id().id() == 200);
  CHECK(loaded->journal[1].event.id().id() == 201);
  CHECK(loaded->journal[0].recipients.empty());
  CHECK(loaded->journal[1].recipients.empty());

  auto* rebuilt_base = loaded->base.Load().as<LeafNode>();
  CHECK(rebuilt_base != nullptr);
  CHECK(rebuilt_base->name == "Root");
  CHECK(!rebuilt_base->base.is_valid());
  CHECK(rebuilt_base->journal.empty());

  return EXIT_SUCCESS;
}
