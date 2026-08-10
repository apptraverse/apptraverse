#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

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

  std::int32_t value{0};
  std::string label;

  void Apply(AddEvent const& event);

  void CaptureBaseStateForTest() { CaptureBaseState(); }

  void InsertAtForTest(std::uint64_t timestamp_us, Event::ptr event) {
    InsertEvent(EventRecord{timestamp_us, std::move(event)});
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

void TestEventApplication() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};

  auto doc = CounterDocument::ptr::Create(ae::CreateWith{domain}.with_id(1));
  auto event = AddEvent::ptr::Create(ae::CreateWith{domain}.with_id(2));
  event->delta = 7;
  event->tag = "a";

  event->ApplyTo(*doc);
  CHECK(doc->value == 7);
  CHECK(doc->label == "a");
  CHECK(doc->journal.empty());
}

void TestJournalCommitAndReplay() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};

  auto base = CounterDocument::ptr::Create(ae::CreateWith{domain}.with_id(10));
  base->value = 1;
  base->label = "b";

  auto doc = CounterDocument::ptr::Create(ae::CreateWith{domain}.with_id(11));
  doc->base = base;
  doc->value = 1;
  doc->label = "b";
  doc->CaptureBaseStateForTest();

  auto e1 = AddEvent::ptr::Create(ae::CreateWith{domain}.with_id(12));
  e1->delta = 2;
  e1->tag = "x";
  doc->Commit(e1);

  while (apptraverse::SystemUtcMicros() <= doc->journal[0].timestamp_us) {
    std::this_thread::sleep_for(std::chrono::microseconds(1));
  }

  auto e2 = AddEvent::ptr::Create(ae::CreateWith{domain}.with_id(13));
  e2->delta = 3;
  e2->tag = "y";
  doc->Commit(e2);

  CHECK(doc->journal.size() == 2);
  CHECK(doc->journal[0].timestamp_us != 0);
  CHECK(doc->journal[1].timestamp_us != 0);
  CHECK(doc->journal[0].timestamp_us < doc->journal[1].timestamp_us);
  CHECK(doc->value == 6);
  CHECK(doc->label == "bxy");

  auto early = AddEvent::ptr::Create(ae::CreateWith{domain}.with_id(14));
  early->delta = 10;
  early->tag = "z";
  doc->InsertAtForTest(doc->journal[0].timestamp_us - 1, early);

  CHECK(doc->journal.size() == 3);
  CHECK(doc->journal[0].event.id().id() == 14);
  CHECK(doc->value == 16);
  CHECK(doc->label == "bzxy");
}

void TestSaveLoad() {
  ae::RamDomainStorage storage;
  ae::ObjId::Type const doc_id = 21;

  {
    ae::Domain domain{ae::Now(), storage};
    auto base =
        CounterDocument::ptr::Create(ae::CreateWith{domain}.with_id(20));
    base->value = 5;
    base->label = "s";

    auto doc =
        CounterDocument::ptr::Create(ae::CreateWith{domain}.with_id(doc_id));
    doc->base = base;
    doc->value = 5;
    doc->label = "s";
    doc->CaptureBaseStateForTest();

    auto event = AddEvent::ptr::Create(ae::CreateWith{domain}.with_id(22));
    event->delta = 4;
    event->tag = "t";
    doc->Commit(event);

    CHECK(doc->value == 9);
    CHECK(doc->label == "st");
    doc.Save();
  }

  {
    ae::Domain domain{ae::Now(), storage};
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

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestEventApplication();
  apptraverse::test::TestJournalCommitAndReplay();
  apptraverse::test::TestSaveLoad();
  std::cout << "event_sourced_core_test OK\n";
  return 0;
}
