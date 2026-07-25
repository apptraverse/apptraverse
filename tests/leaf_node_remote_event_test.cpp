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

  void CommitEventForTest(apptraverse::Event::ptr event, ae::TimePoint time) {
    CommitEvent(std::move(event), time);
  }

  void AcceptRemoteEventForTest(apptraverse::Event::ptr event,
                                ae::TimePoint time) {
    AcceptRemoteEvent(std::move(event), time);
  }

  void RebuildFromBaseAndReplayForTest() { RebuildFromBaseAndReplay(); }
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
  using apptraverse::EventRecordOrigin;
  using apptraverse::test::AppendLeafNodeNameEvent;
  using apptraverse::test::LeafNode;

  ae::RamDomainStorage storage;

  ae::Domain domain1{ae::Now(), storage};

  LeafNode::ptr base =
      LeafNode::ptr::Create(ae::CreateWith{domain1}.with_id(1000));
  CHECK(static_cast<bool>(base));
  base->name = "Uninitialized base";
  CHECK(!base->base.is_valid());
  CHECK(base->journal.empty());

  LeafNode::ptr live =
      LeafNode::ptr::Create(ae::CreateWith{domain1}.with_id(100));
  CHECK(static_cast<bool>(live));
  live->name = "Alice";
  live->base = base;
  CHECK(live->journal.empty());
  CHECK(live->base.is_valid());
  CHECK(live->base.is_loaded());

  live->CaptureBaseStateForTest();

  CHECK(live->name == "Alice");
  CHECK(live.id().id() == 100);
  CHECK(live->base.id().id() == 1000);
  CHECK(live->journal.empty());

  auto* captured_base = live->base.Load().as<LeafNode>();
  CHECK(captured_base != nullptr);
  CHECK(captured_base->name == "Alice");
  CHECK(!captured_base->base.is_valid());
  CHECK(captured_base->journal.empty());

  AppendLeafNodeNameEvent::ptr local_event =
      AppendLeafNodeNameEvent::ptr::Create(ae::CreateWith{domain1}.with_id(200));
  CHECK(static_cast<bool>(local_event));
  local_event->suffix = " Cooper";

  ae::TimePoint const local_time{std::chrono::microseconds{200}};
  live->CommitEventForTest(local_event, local_time);

  CHECK(live->name == "Alice Cooper");
  CHECK(live->journal.size() == 1);
  CHECK(live->journal[0].event.id().id() == 200);
  CHECK(live->journal[0].time == local_time);
  CHECK(live->journal[0].origin == EventRecordOrigin::kLocal);
  CHECK(live->journal[0].recipients.empty());

  AppendLeafNodeNameEvent::ptr earlier_remote_event =
      AppendLeafNodeNameEvent::ptr::Create(ae::CreateWith{domain1}.with_id(201));
  CHECK(static_cast<bool>(earlier_remote_event));
  earlier_remote_event->suffix = " B.";

  ae::TimePoint const earlier_remote_time{std::chrono::microseconds{100}};
  live->AcceptRemoteEventForTest(earlier_remote_event, earlier_remote_time);

  CHECK(live->name == "Alice B. Cooper");
  CHECK(live.id().id() == 100);
  CHECK(live->base.id().id() == 1000);
  CHECK(live->journal.size() == 2);

  CHECK(live->journal[0].event.id().id() == 201);
  CHECK(live->journal[0].time == earlier_remote_time);
  CHECK(live->journal[0].origin == EventRecordOrigin::kRemote);
  CHECK(live->journal[0].recipients.empty());

  CHECK(live->journal[1].event.id().id() == 200);
  CHECK(live->journal[1].time == local_time);
  CHECK(live->journal[1].origin == EventRecordOrigin::kLocal);
  CHECK(live->journal[1].recipients.empty());

  auto* base_after_early = live->base.Load().as<LeafNode>();
  CHECK(base_after_early != nullptr);
  CHECK(base_after_early->name == "Alice");
  CHECK(!base_after_early->base.is_valid());
  CHECK(base_after_early->journal.empty());

  AppendLeafNodeNameEvent::ptr later_remote_event =
      AppendLeafNodeNameEvent::ptr::Create(ae::CreateWith{domain1}.with_id(202));
  CHECK(static_cast<bool>(later_remote_event));
  later_remote_event->suffix = " Jr.";

  ae::TimePoint const later_remote_time{std::chrono::microseconds{300}};
  live->AcceptRemoteEventForTest(later_remote_event, later_remote_time);

  CHECK(live->name == "Alice B. Cooper Jr.");
  CHECK(live->name != "Alice B. Cooper B. Cooper Jr.");
  CHECK(live->name != "Alice B. Cooper Jr. Jr.");
  CHECK(live->journal.size() == 3);
  CHECK(live->journal[2].event.id().id() == 202);
  CHECK(live->journal[2].time == later_remote_time);
  CHECK(live->journal[2].origin == EventRecordOrigin::kRemote);
  CHECK(live->journal[2].recipients.empty());

  CHECK(live->journal[0].time == earlier_remote_time);
  CHECK(live->journal[1].time == local_time);
  CHECK(live->journal[2].time == later_remote_time);
  CHECK(live->journal[0].origin == EventRecordOrigin::kRemote);
  CHECK(live->journal[0].recipients.empty());
  CHECK(live->journal[1].origin == EventRecordOrigin::kLocal);
  CHECK(live->journal[1].recipients.empty());
  CHECK(live->journal[2].origin == EventRecordOrigin::kRemote);
  CHECK(live->journal[2].recipients.empty());

  live.Save();

  ae::Domain domain2{ae::Now(), storage};
  LeafNode::ptr loaded =
      LeafNode::ptr::Declare(ae::CreateWith{domain2}.with_id(100));
  loaded.Load();

  CHECK(static_cast<bool>(loaded));
  CHECK(loaded.id().id() == 100);
  CHECK(loaded->name == "Alice B. Cooper Jr.");
  CHECK(loaded->base.id().id() == 1000);
  CHECK(loaded->journal.size() == 3);

  CHECK(loaded->journal[0].event.id().id() == 201);
  CHECK(loaded->journal[0].time == earlier_remote_time);
  CHECK(loaded->journal[0].origin == EventRecordOrigin::kRemote);
  CHECK(loaded->journal[0].recipients.empty());
  CHECK(loaded->journal[0].event.is_valid());
  CHECK(loaded->journal[0].event.is_loaded());
  CHECK(loaded->journal[0].event->GetClassId() ==
        AppendLeafNodeNameEvent::kClassId);
  auto* loaded_earlier =
      loaded->journal[0].event.Load().as<AppendLeafNodeNameEvent>();
  CHECK(loaded_earlier != nullptr);
  CHECK(loaded_earlier->suffix == " B.");

  CHECK(loaded->journal[1].event.id().id() == 200);
  CHECK(loaded->journal[1].time == local_time);
  CHECK(loaded->journal[1].origin == EventRecordOrigin::kLocal);
  CHECK(loaded->journal[1].recipients.empty());
  CHECK(loaded->journal[1].event.is_valid());
  CHECK(loaded->journal[1].event.is_loaded());
  CHECK(loaded->journal[1].event->GetClassId() ==
        AppendLeafNodeNameEvent::kClassId);
  auto* loaded_local =
      loaded->journal[1].event.Load().as<AppendLeafNodeNameEvent>();
  CHECK(loaded_local != nullptr);
  CHECK(loaded_local->suffix == " Cooper");

  CHECK(loaded->journal[2].event.id().id() == 202);
  CHECK(loaded->journal[2].time == later_remote_time);
  CHECK(loaded->journal[2].origin == EventRecordOrigin::kRemote);
  CHECK(loaded->journal[2].recipients.empty());
  CHECK(loaded->journal[2].event.is_valid());
  CHECK(loaded->journal[2].event.is_loaded());
  CHECK(loaded->journal[2].event->GetClassId() ==
        AppendLeafNodeNameEvent::kClassId);
  auto* loaded_later =
      loaded->journal[2].event.Load().as<AppendLeafNodeNameEvent>();
  CHECK(loaded_later != nullptr);
  CHECK(loaded_later->suffix == " Jr.");

  auto* loaded_base = loaded->base.Load().as<LeafNode>();
  CHECK(loaded_base != nullptr);
  CHECK(loaded_base->name == "Alice");
  CHECK(!loaded_base->base.is_valid());
  CHECK(loaded_base->journal.empty());

  loaded->name = "Transient value";
  loaded->RebuildFromBaseAndReplayForTest();

  CHECK(loaded->name == "Alice B. Cooper Jr.");
  CHECK(loaded.id().id() == 100);
  CHECK(loaded->base.id().id() == 1000);
  CHECK(loaded->journal.size() == 3);
  CHECK(loaded->journal[0].event.id().id() == 201);
  CHECK(loaded->journal[0].time == earlier_remote_time);
  CHECK(loaded->journal[0].origin == EventRecordOrigin::kRemote);
  CHECK(loaded->journal[0].recipients.empty());
  CHECK(loaded->journal[1].event.id().id() == 200);
  CHECK(loaded->journal[1].time == local_time);
  CHECK(loaded->journal[1].origin == EventRecordOrigin::kLocal);
  CHECK(loaded->journal[1].recipients.empty());
  CHECK(loaded->journal[2].event.id().id() == 202);
  CHECK(loaded->journal[2].time == later_remote_time);
  CHECK(loaded->journal[2].origin == EventRecordOrigin::kRemote);
  CHECK(loaded->journal[2].recipients.empty());

  auto* rebuilt_base = loaded->base.Load().as<LeafNode>();
  CHECK(rebuilt_base != nullptr);
  CHECK(rebuilt_base->name == "Alice");
  CHECK(!rebuilt_base->base.is_valid());
  CHECK(rebuilt_base->journal.empty());

  return EXIT_SUCCESS;
}
