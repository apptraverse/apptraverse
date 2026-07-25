#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/event_record.h"
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

  void ReplayJournalForTest() { ReplayJournal(); }

  void RebuildFromBaseAndReplayForTest() { RebuildFromBaseAndReplay(); }

  void CaptureBaseStateForTest() { CaptureBaseState(); }
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

void PrepareCommittedEvent(apptraverse::Event::ptr event,
                           ae::Obj::ptr sender, std::uint32_t sequence) {
  event->sender = sender;
  event->sequence = sequence;
  event->sender.Reset();
  event->sender.SetFlags(ae::ObjFlags::kUnloadedByDefault);
}

}  // namespace

int main() {
  using apptraverse::EventRecord;
  using apptraverse::test::AppendLeafNodeNameEvent;
  using apptraverse::test::LeafNode;

  ae::RamDomainStorage storage;

  ae::Domain domain1{ae::Now(), storage};

  LeafNode::ptr base =
      LeafNode::ptr::Create(ae::CreateWith{domain1}.with_id(1000));
  CHECK(static_cast<bool>(base));
  base->name = "Root";
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

  AppendLeafNodeNameEvent::ptr later_event =
      AppendLeafNodeNameEvent::ptr::Create(ae::CreateWith{domain1}.with_id(200));
  CHECK(static_cast<bool>(later_event));
  later_event->suffix = " Value";

  ae::TimePoint const later_time{std::chrono::microseconds{200}};
  PrepareCommittedEvent(later_event, live, 2);
  live->journal.push_back(EventRecord{later_event, later_time, {}});

  live->ReplayJournalForTest();

  CHECK(live->name == "Root Value");
  CHECK(live->journal.size() == 1);
  CHECK(live->journal[0].event.id().id() == 200);

  AppendLeafNodeNameEvent::ptr earlier_event =
      AppendLeafNodeNameEvent::ptr::Create(ae::CreateWith{domain1}.with_id(201));
  CHECK(static_cast<bool>(earlier_event));
  earlier_event->suffix = " B.";

  ae::TimePoint const earlier_time{std::chrono::microseconds{100}};
  PrepareCommittedEvent(earlier_event, live, 1);
  live->journal.insert(live->journal.begin(),
                       EventRecord{earlier_event, earlier_time, {}});

  CHECK(live->name == "Root Value");
  CHECK(live->journal.size() == 2);
  CHECK(live->journal[0].event.id().id() == 201);
  CHECK(live->journal[1].event.id().id() == 200);

  live->RebuildFromBaseAndReplayForTest();

  CHECK(live->name == "Root B. Value");
  CHECK(live.id().id() == 100);
  CHECK(live->base.id().id() == 1000);
  CHECK(live->journal.size() == 2);
  CHECK(live->journal[0].event.id().id() == 201);
  CHECK(live->journal[1].event.id().id() == 200);
  CHECK(live->journal[0].time == earlier_time);
  CHECK(live->journal[1].time == later_time);

  live.Save();

  ae::Domain domain2{ae::Now(), storage};
  LeafNode::ptr loaded =
      LeafNode::ptr::Declare(ae::CreateWith{domain2}.with_id(100));
  CHECK(loaded.is_valid());
  CHECK(!loaded.is_loaded());

  loaded.Load();

  CHECK(static_cast<bool>(loaded));
  CHECK(loaded.id().id() == 100);
  CHECK(loaded->name == "Root B. Value");
  CHECK(loaded->base.id().id() == 1000);
  CHECK(loaded->journal.size() == 2);

  CHECK(loaded->journal[0].event.id().id() == 201);
  CHECK(loaded->journal[0].time == earlier_time);
  CHECK(loaded->journal[1].event.id().id() == 200);
  CHECK(loaded->journal[1].time == later_time);

  CHECK(loaded->journal[0].event.is_valid());
  CHECK(loaded->journal[0].event.is_loaded());
  CHECK(loaded->journal[1].event.is_valid());
  CHECK(loaded->journal[1].event.is_loaded());

  auto* earlier_loaded =
      loaded->journal[0].event.Load().as<AppendLeafNodeNameEvent>();
  CHECK(earlier_loaded != nullptr);
  CHECK(earlier_loaded->suffix == " B.");
  CHECK(loaded->journal[0].event->GetClassId() ==
        AppendLeafNodeNameEvent::kClassId);

  auto* later_loaded =
      loaded->journal[1].event.Load().as<AppendLeafNodeNameEvent>();
  CHECK(later_loaded != nullptr);
  CHECK(later_loaded->suffix == " Value");
  CHECK(loaded->journal[1].event->GetClassId() ==
        AppendLeafNodeNameEvent::kClassId);

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
  CHECK(loaded->journal.size() == 2);

  return EXIT_SUCCESS;
}
