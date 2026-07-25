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

class RenameLeafNodeEvent
    : public apptraverse::EventFor<LeafNode, RenameLeafNodeEvent> {
  AE_OBJECT(RenameLeafNodeEvent, Event, 0)

 protected:
  RenameLeafNodeEvent() = default;

 public:
  explicit RenameLeafNodeEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name))

  std::string name;
};

class LeafNode : public apptraverse::NodeFor<LeafNode> {
  AE_OBJECT(LeafNode, Node, 0)

 protected:
  LeafNode() = default;

 public:
  explicit LeafNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name))

  std::string name;

  void Apply(RenameLeafNodeEvent const& event) { name = event.name; }

  void ReplayJournalForTest() { ReplayJournal(); }

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
  using apptraverse::test::LeafNode;
  using apptraverse::test::RenameLeafNodeEvent;

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

  RenameLeafNodeEvent::ptr rename_event =
      RenameLeafNodeEvent::ptr::Create(ae::CreateWith{domain1}.with_id(200));
  CHECK(static_cast<bool>(rename_event));
  rename_event->name = "Root Value";

  ae::TimePoint const event_time{std::chrono::microseconds{123456}};
  PrepareCommittedEvent(rename_event, live, 1);
  live->journal.push_back(EventRecord{rename_event, event_time, {}});

  live->ReplayJournalForTest();

  CHECK(live->name == "Root Value");
  CHECK(live.id().id() == 100);
  CHECK(live->base.id().id() == 1000);
  CHECK(live->journal.size() == 1);

  live.Save();

  ae::Domain domain2{ae::Now(), storage};
  LeafNode::ptr loaded =
      LeafNode::ptr::Declare(ae::CreateWith{domain2}.with_id(100));
  CHECK(loaded.is_valid());
  CHECK(!loaded.is_loaded());

  loaded.Load();

  CHECK(static_cast<bool>(loaded));
  CHECK(loaded.is_loaded());
  CHECK(loaded.id().id() == 100);
  CHECK(loaded->name == "Root Value");
  CHECK(loaded->base.is_valid());
  CHECK(loaded->base.is_loaded());
  CHECK(loaded->base.id().id() == 1000);

  auto* loaded_base = loaded->base.Load().as<LeafNode>();
  CHECK(loaded_base != nullptr);
  CHECK(loaded_base->name == "Root");
  CHECK(!loaded_base->base.is_valid());
  CHECK(loaded_base->journal.empty());

  CHECK(loaded->journal.size() == 1);
  CHECK(loaded->journal[0].time == event_time);
  CHECK(loaded->journal[0].recipients.empty());
  CHECK(loaded->journal[0].event.is_valid());
  CHECK(loaded->journal[0].event.is_loaded());
  CHECK(loaded->journal[0].event.id().id() == 200);
  CHECK(loaded->journal[0].event->GetClassId() ==
        RenameLeafNodeEvent::kClassId);

  auto* loaded_event =
      loaded->journal[0].event.Load().as<RenameLeafNodeEvent>();
  CHECK(loaded_event != nullptr);
  CHECK(loaded_event->name == "Root Value");

  loaded->name = "Transient value";
  loaded->RebuildFromBaseAndReplayForTest();

  CHECK(loaded.id().id() == 100);
  CHECK(loaded->name == "Root Value");
  CHECK(loaded->base.id().id() == 1000);
  CHECK(loaded->journal.size() == 1);
  CHECK(loaded->journal[0].event.id().id() == 200);

  auto* rebuilt_base = loaded->base.Load().as<LeafNode>();
  CHECK(rebuilt_base != nullptr);
  CHECK(rebuilt_base->name == "Root");
  CHECK(!rebuilt_base->base.is_valid());
  CHECK(rebuilt_base->journal.empty());

  return EXIT_SUCCESS;
}
