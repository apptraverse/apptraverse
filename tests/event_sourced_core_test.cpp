#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "aether/clock.h"
#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/event.h"
#include "apptraverse/event_for.h"
#include "apptraverse/node.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/presenter.h"
#include "apptraverse/runtime_lifecycle.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/shared_event_order.h"

namespace apptraverse::test {

class CounterDocument;
class AddEvent;

class CounterDocument : public apptraverse::NodeFor<CounterDocument> {
  // Version 1: own Load/Save so CaptureBaseState/Rebuild keep value/label.
  // (Inheriting only Node's versioned Save would drop derived fields.)
  APPTRAVERSE_OBJECT(CounterDocument, Node, 1)

 protected:
  CounterDocument() = default;

 public:
  explicit CounterDocument(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value), AE_MMBR(label))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv&) {
    throw std::runtime_error("CounterDocument v0 is not supported");
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    Node::Load(ae::Version<1>{}, dnv);
    dnv(value, label);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    Node::Save(ae::Version<1>{}, dnv);
    dnv(value, label);
  }

  std::int32_t value{0};
  std::string label;

  void Apply(AddEvent const& event);

  void InsertAtForTest(SharedEventOrder order, Event::ptr event) {
    InsertEvent(EventRecord{.event = std::move(event),
                            .identity = {},
                            .order = std::move(order)});
  }

  void InsertAtForTest(std::uint64_t lamport, Event::ptr event) {
    InsertAtForTest(SharedEventOrder{.lamport = lamport}, std::move(event));
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
  CHECK(doc->journal[0].order.lamport != 0);
  CHECK(doc->journal[1].order.lamport != 0);
  CHECK(doc->journal[0].order.lamport < doc->journal[1].order.lamport);
  CHECK(doc->value == 6);
  CHECK(doc->label == "bxy");

  auto early = AddEvent::ptr::Create(ae::CreateWith{domain}.with_id(14));
  early->delta = 10;
  early->tag = "z";
  doc->InsertAtForTest(doc->journal[0].order.lamport - 1, early);

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
    CHECK(doc->journal[i - 1].order.lamport < doc->journal[i].order.lamport);
  }
  CHECK(doc->value == 20);
}

void TestStableClassIdsAreUnique() {
  using apptraverse::Event;
  using apptraverse::Node;
  using apptraverse::Presenter;
  using apptraverse::ApplicationRuntimeState;
  using apptraverse::NetworkState;
  using apptraverse::AetherRegistrationState;
  using apptraverse::ApplicationStartedEvent;
  using apptraverse::AetherRegistrationCompletedEvent;

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

  auto const named = crc32::from_literal("chat::ChatClient").value;
  CHECK(named != Node::kClassId);
  CHECK(named != Event::kClassId);
  CHECK(named != CounterDocument::kClassId);

  CHECK(ApplicationRuntimeState::kClassId ==
        crc32::from_literal("apptraverse::ApplicationRuntimeState").value);
  CHECK(NetworkState::kClassId ==
        crc32::from_literal("apptraverse::NetworkState").value);
  CHECK(AetherRegistrationState::kClassId ==
        crc32::from_literal("apptraverse::AetherRegistrationState").value);
  CHECK(ApplicationStartedEvent::kClassId ==
        crc32::from_literal("apptraverse::ApplicationStartedEvent").value);
  CHECK(AetherRegistrationCompletedEvent::kClassId ==
        crc32::from_literal("apptraverse::AetherRegistrationCompletedEvent")
            .value);
  CHECK(ApplicationRuntimeState::kClassId != Node::kClassId);
  CHECK(NetworkState::kClassId != ApplicationRuntimeState::kClassId);
  CHECK(AetherRegistrationState::kClassId != NetworkState::kClassId);
}

void TestRuntimeSessionResetsObservations() {
  using apptraverse::AetherRegistrationPhase;
  using apptraverse::AetherRegistrationState;
  using apptraverse::ApplicationRuntimeState;
  using apptraverse::BeginRuntimeSession;
  using apptraverse::CommitAetherRegistrationCompleted;
  using apptraverse::CommitNetworkAvailable;
  using apptraverse::CommitNetworkInterfaceUnavailable;
  using apptraverse::InitializeRuntimeNode;
  using apptraverse::NetworkAvailability;
  using apptraverse::NetworkState;
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto runtime = ApplicationRuntimeState::ptr::Create(ae::CreateWith{domain});
  auto network = NetworkState::ptr::Create(ae::CreateWith{domain});
  auto aether = AetherRegistrationState::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*runtime);
  InitializeRuntimeNode(*network);
  InitializeRuntimeNode(*aether);

  BeginRuntimeSession(*runtime, *network, *aether);
  CHECK(runtime->run_id == 1);
  CHECK(network->GetAvailability() == NetworkAvailability::kInitializing);
  CHECK(aether->GetPhase() == AetherRegistrationPhase::kRegistering);
  CHECK(!aether->IsRegisteredForCurrentRun());
  CHECK(runtime->journal.size() == 1);
  CHECK(network->journal.size() == 1);
  CHECK(aether->journal.size() == 1);

  CHECK(CommitNetworkAvailable(*network, runtime->run_id));
  CHECK(CommitAetherRegistrationCompleted(*aether, "abc-uid"));
  CHECK(aether->IsRegisteredForCurrentRun());
  CHECK(aether->CurrentUid() == "abc-uid");
  CHECK(network->GetAvailability() == NetworkAvailability::kAvailable);

  BeginRuntimeSession(*runtime, *network, *aether);
  CHECK(runtime->run_id == 2);
  CHECK(network->GetAvailability() == NetworkAvailability::kInitializing);
  CHECK(aether->GetPhase() == AetherRegistrationPhase::kRegistering);
  CHECK(!aether->IsRegisteredForCurrentRun());
  CHECK(aether->CurrentUid().empty());
  CHECK(aether->uid == "abc-uid");

  CHECK(CommitNetworkInterfaceUnavailable(*network, runtime->run_id));
  CHECK(network->GetAvailability() ==
        NetworkAvailability::kInterfaceUnavailable);
  CHECK(!CommitNetworkInterfaceUnavailable(*network, runtime->run_id));
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestJournalCommitAndReplay();
  apptraverse::test::TestSaveLoad();
  apptraverse::test::TestStableClassIdsAreUnique();
  apptraverse::test::TestRuntimeSessionResetsObservations();
  apptraverse::test::TestMonotonicTimestampWithoutSleep();
  std::cout << "event_sourced_core_test OK\n";
  return 0;
}
