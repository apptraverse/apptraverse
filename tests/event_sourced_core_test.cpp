#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/event.h"
#include "apptraverse/event_for.h"
#include "apptraverse/event_order.h"
#include "apptraverse/node.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/presenter.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse::test {

class CounterDocument;
class AddEvent;

class CounterDocument : public apptraverse::NodeFor<CounterDocument> {
  APPTRAVERSE_OBJECT(CounterDocument, Node, 0)

 protected:
  CounterDocument() = default;

 public:
  explicit CounterDocument(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value), AE_MMBR(label))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, value, label);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, value, label);
  }

  std::int32_t value{0};
  std::string label;

  void Apply(AddEvent const& event);

  void InsertAtForTest(EventOrder order, Event::ptr event) {
    InsertEvent(EventRecord{.event = std::move(event),
                            .order = std::move(order)});
  }

  void InsertAtForTest(std::uint64_t timestamp_us, Event::ptr event) {
    InsertAtForTest(EventOrder{.timestamp_us = timestamp_us}, std::move(event));
  }
};

class AddEvent : public apptraverse::EventFor<CounterDocument, AddEvent> {
  APPTRAVERSE_OBJECT(AddEvent, Event, 0)

 protected:
  AddEvent() = default;

 public:
  explicit AddEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(delta), AE_MMBR(tag))

  std::int32_t delta{0};
  std::string tag;
};

APPTRAVERSE_REGISTER(CounterDocument);
APPTRAVERSE_REGISTER(AddEvent);

void CounterDocument::Apply(AddEvent const& event) {
  value += event.delta;
  label += event.tag;
}

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

void TestJournalCommitAndReplay() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};

  auto base = CounterDocument::ptr::Create(ae::CreateWith{domain}.with_id(10));
  base->value = 1;
  base->label = "b";

  auto doc = CounterDocument::ptr::Create(ae::CreateWith{domain}.with_id(11));
  doc->base = base;
  doc->value = 1;
  doc->label = "b";
  doc->CaptureBaseState();

  auto e1 = AddEvent::ptr::Create(ae::CreateWith{domain}.with_id(12));
  e1->delta = 2;
  e1->tag = "x";
  doc->Commit(e1);

  auto e2 = AddEvent::ptr::Create(ae::CreateWith{domain}.with_id(13));
  e2->delta = 3;
  e2->tag = "y";
  doc->Commit(e2);

  CHECK(doc->journal.size() == 2);
  CHECK(doc->journal[0].order.timestamp_us != 0);
  CHECK(doc->journal[1].order.timestamp_us != 0);
  CHECK(doc->journal[0].order.timestamp_us < doc->journal[1].order.timestamp_us);
  CHECK(doc->value == 6);
  CHECK(doc->label == "bxy");

  auto early = AddEvent::ptr::Create(ae::CreateWith{domain}.with_id(14));
  early->delta = 10;
  early->tag = "z";
  doc->InsertAtForTest(doc->journal[0].order.timestamp_us - 1, early);

  CHECK(doc->journal.size() == 3);
  CHECK(doc->journal[0].event.id().id() == 14);
  CHECK(doc->value == 16);
  CHECK(doc->label == "bzxy");
}

void TestSaveLoad() {
  ae::RamDomainStorage storage;
  ae::ObjId::Type const doc_id = 21;

  {
    ae::Domain domain{storage};
    auto base =
        CounterDocument::ptr::Create(ae::CreateWith{domain}.with_id(20));
    base->value = 5;
    base->label = "s";

    auto doc =
        CounterDocument::ptr::Create(ae::CreateWith{domain}.with_id(doc_id));
    doc->base = base;
    doc->value = 5;
    doc->label = "s";
    doc->CaptureBaseState();

    auto event = AddEvent::ptr::Create(ae::CreateWith{domain}.with_id(22));
    event->delta = 4;
    event->tag = "t";
    doc->Commit(event);

    CHECK(doc->value == 9);
    CHECK(doc->label == "st");
    doc.Save();
  }

  {
    ae::Domain domain{storage};
    auto doc =
        CounterDocument::ptr::Declare(ae::CreateWith{domain}.with_id(doc_id));
    doc.Load();
    CHECK(doc.is_loaded());
    CHECK(doc->journal.size() == 1);
    CHECK(doc->value == 9);
    CHECK(doc->label == "st");
    CHECK(doc->base.is_valid());
    auto base = CounterDocument::ptr{doc->base};
    base.Load();
    CHECK(base.is_loaded());
    CHECK(base->value == 5);
  }
}

void TestMonotonicTimestampWithoutSleep() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};

  auto base = CounterDocument::ptr::Create(ae::CreateWith{domain}.with_id(30));
  auto doc = CounterDocument::ptr::Create(ae::CreateWith{domain}.with_id(31));
  doc->base = base;
  doc->CaptureBaseState();

  for (int i = 0; i < 20; ++i) {
    auto event = AddEvent::ptr::Create(ae::CreateWith{domain});
    event->delta = 1;
    event->tag = ".";
    doc->Commit(event);
  }
  CHECK(doc->journal.size() == 20);
  for (std::size_t i = 1; i < doc->journal.size(); ++i) {
    CHECK(doc->journal[i - 1].order.timestamp_us <
          doc->journal[i].order.timestamp_us);
  }
  CHECK(doc->value == 20);
}

void TestOrderUsesTimestampOnly() {
  EventRecord const early{.order = EventOrder{.timestamp_us = 100}};
  EventRecord const late{.order = EventOrder{.timestamp_us = 200}};
  CHECK(EventRecordOrderLess(early, late));
  CHECK(!EventRecordOrderLess(late, early));

  EventRecord const same_a{.order = EventOrder{.timestamp_us = 500}};
  EventRecord const same_b{.order = EventOrder{.timestamp_us = 500}};
  CHECK(!EventRecordOrderLess(same_a, same_b));
  CHECK(!EventRecordOrderLess(same_b, same_a));
  CHECK(same_a.order == same_b.order);
}

void TestMidJournalEventReplaysByTimestamp() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};

  auto base = CounterDocument::ptr::Create(ae::CreateWith{domain}.with_id(40));
  base->label = "b";
  auto doc = CounterDocument::ptr::Create(ae::CreateWith{domain}.with_id(41));
  doc->base = base;
  doc->label = "b";
  doc->CaptureBaseState();

  auto const add = [&](std::int32_t delta, std::string tag) {
    auto event = AddEvent::ptr::Create(ae::CreateWith{domain});
    event->delta = delta;
    event->tag = std::move(tag);
    return event;
  };

  doc->InsertAtForTest(100, add(1, "1"));
  doc->InsertAtForTest(300, add(3, "3"));
  CHECK(doc->label == "b13");

  doc->InsertAtForTest(200, add(2, "2"));

  CHECK(doc->journal.size() == 3);
  CHECK(doc->journal[0].order.timestamp_us == 100);
  CHECK(doc->journal[1].order.timestamp_us == 200);
  CHECK(doc->journal[2].order.timestamp_us == 300);
  CHECK(doc->label == "b123");
  CHECK(doc->value == 6);
}

void TestStableClassIdsAreUnique() {
  using apptraverse::Event;
  using apptraverse::Node;
  using apptraverse::Presenter;

  CHECK(Node::kClassId == crc32::from_literal("apptraverse::Node").value);
  CHECK(Event::kClassId == crc32::from_literal("apptraverse::Event").value);
  CHECK(Presenter::kClassId ==
        crc32::from_literal("apptraverse::Presenter").value);
  CHECK(CounterDocument::kClassId ==
        crc32::from_literal("apptraverse::CounterDocument").value);
  CHECK(AddEvent::kClassId ==
        crc32::from_literal("apptraverse::AddEvent").value);

  CHECK(Node::kClassId != Event::kClassId);
  CHECK(Node::kClassId != Presenter::kClassId);
  CHECK(Event::kClassId != Presenter::kClassId);
  CHECK(CounterDocument::kClassId != Node::kClassId);
  CHECK(AddEvent::kClassId != Event::kClassId);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestJournalCommitAndReplay();
  apptraverse::test::TestSaveLoad();
  apptraverse::test::TestStableClassIdsAreUnique();
  apptraverse::test::TestMonotonicTimestampWithoutSleep();
  apptraverse::test::TestOrderUsesTimestampOnly();
  apptraverse::test::TestMidJournalEventReplaysByTimestamp();
  std::cout << "event_sourced_core_test OK\n";
  return 0;
}
