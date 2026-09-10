#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/event_for.h"
#include "apptraverse/journal_retention_policy.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/shared_event_id.h"
#include "apptraverse/shared_event_order.h"

namespace apptraverse::test {
namespace {

class RetentionDoc;
class AddEvent;

class RetentionDoc : public NodeFor<RetentionDoc> {
  APPTRAVERSE_OBJECT(RetentionDoc, Node, 2)

 protected:
  RetentionDoc() = default;

 public:
  explicit RetentionDoc(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv&) {
    throw std::runtime_error("RetentionDoc v0 is not supported");
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    Node::Load(ae::Version<1>{}, dnv);
    dnv(value);
  }

  template <typename Dnv>
  void Load(ae::Version<2>, Dnv& dnv) {
    Node::Load(ae::Version<2>{}, dnv);
    dnv(value);
  }

  template <typename Dnv>
  void Save(ae::Version<2>, Dnv& dnv) const {
    Node::Save(ae::Version<2>{}, dnv);
    dnv(value);
  }

  std::int32_t value{0};

  void Apply(AddEvent const& event);

  void InsertSharedForTest(Event::ptr event, SharedEventId identity,
                           SharedEventOrder order,
                           std::uint64_t retained_since_us) {
    InsertEvent(EventRecord{.event = std::move(event),
                            .identity = std::move(identity),
                            .order = std::move(order),
                            .retained_since_us = retained_since_us});
  }

  void InsertLocalForTest(Event::ptr event, std::uint64_t lamport,
                          std::uint64_t retained_since_us) {
    InsertEvent(EventRecord{
        .event = std::move(event),
        .identity = {},
        .order = SharedEventOrder{.lamport = lamport},
        .retained_since_us = retained_since_us});
  }
};

class AddEvent : public EventFor<RetentionDoc, AddEvent> {
  APPTRAVERSE_OBJECT(AddEvent, Event, 0)

 protected:
  AddEvent() = default;

 public:
  explicit AddEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(delta))

  std::int32_t delta{0};
};

class LegacyRetentionDoc;
class LegacyAddEvent;

class LegacyRetentionDoc : public NodeFor<LegacyRetentionDoc> {
  APPTRAVERSE_OBJECT(LegacyRetentionDoc, Node, 1)

 protected:
  LegacyRetentionDoc() = default;

 public:
  explicit LegacyRetentionDoc(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv&) {
    throw std::runtime_error("LegacyRetentionDoc v0 is not supported");
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    Node::Load(ae::Version<1>{}, dnv);
    dnv(value);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    Node::Save(ae::Version<1>{}, dnv);
    dnv(value);
  }

  std::int32_t value{0};

  void Apply(LegacyAddEvent const& event);
};

class LegacyAddEvent : public EventFor<LegacyRetentionDoc, LegacyAddEvent> {
  APPTRAVERSE_OBJECT(LegacyAddEvent, Event, 0)

 protected:
  LegacyAddEvent() = default;

 public:
  explicit LegacyAddEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(delta))

  std::int32_t delta{0};
};

APPTRAVERSE_REGISTER(RetentionDoc);
APPTRAVERSE_REGISTER(AddEvent);
APPTRAVERSE_REGISTER(LegacyRetentionDoc);
APPTRAVERSE_REGISTER(LegacyAddEvent);

void RetentionDoc::Apply(AddEvent const& event) {
  value += event.delta;
  NoteMaterializedChange();
}

void LegacyRetentionDoc::Apply(LegacyAddEvent const& event) {
  value += event.delta;
  NoteMaterializedChange();
}

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

RetentionDoc::ptr MakeDoc(ae::Domain& domain) {
  auto doc = RetentionDoc::ptr::Create(ae::CreateWith{domain});
  doc->value = 0;
  InitializeRuntimeNode(*doc);
  return doc;
}

AddEvent::ptr MakeAdd(ae::Domain& domain, std::int32_t delta) {
  auto event = AddEvent::ptr::Create(ae::CreateWith{domain});
  event->delta = delta;
  return event;
}

std::size_t CountReachableAddEvents(RetentionDoc& doc) {
  std::vector<ae::Obj*> objects;
  CollectReachableObjects(doc, objects);
  std::size_t count = 0;
  for (ae::Obj* obj : objects) {
    if (obj->GetClassId() == AddEvent::kClassId) {
      ++count;
    }
  }
  return count;
}

void TestMaxEventsTen() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto doc = MakeDoc(domain);
  doc->SetJournalRetentionPolicy(JournalRetentionPolicy{.max_events = 10});

  for (int i = 0; i < 100; ++i) {
    doc->Commit(MakeAdd(domain, 1));
  }
  CHECK(doc->value == 100);
  CHECK(doc->journal.size() == 100);
  auto const generation = doc->Generation();
  auto const last_orders = [&] {
    std::vector<SharedEventOrder> orders;
    for (std::size_t i = 90; i < 100; ++i) {
      orders.push_back(doc->journal[i].order);
    }
    return orders;
  }();

  PendingDirtyNodes pending;
  doc->BindMaterializedChangeNotifier(&pending, &PendingDirtyNodesNotify);
  // Compact must not notify even when a notifier is bound.
  doc->CompactJournal(SystemUtcMicros());
  CHECK(pending.empty());
  doc->ClearMaterializedChangeNotifier();
  CHECK(doc->Generation() == generation);
  CHECK(doc->value == 100);
  CHECK(doc->journal.size() == 10);
  CHECK(CountReachableAddEvents(*doc) == 10);
  for (std::size_t i = 0; i < 10; ++i) {
    CHECK(doc->journal[i].order == last_orders[i]);
  }

  doc.Save();
  auto reloaded = RetentionDoc::ptr::Declare(
      ae::CreateWith{domain}.with_id(doc.id()));
  reloaded.Load();
  CHECK(reloaded);
  CHECK(reloaded->value == 100);
  CHECK(reloaded->journal.size() == 10);
}

void TestAgeRetentionInclusive() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto doc = MakeDoc(domain);
  doc->SetJournalRetentionPolicy(JournalRetentionPolicy{
      .max_events = 0,
      .max_age = std::chrono::seconds{25},
  });

  std::uint64_t const times_s[] = {10, 20, 30, 40, 50};
  for (std::size_t i = 0; i < 5; ++i) {
    doc->InsertLocalForTest(MakeAdd(domain, 1), i + 1,
                            times_s[i] * 1'000'000ull);
  }
  CHECK(doc->value == 5);
  auto const generation = doc->Generation();

  // now=60s, max_age=25s => retain if age <= 25 => retained_since >= 35s
  // so 40s and 50s remain (2 records). Boundary: 35s would be kept;
  // 30s age=30 > 25 dropped.
  doc->CompactJournal(60ull * 1'000'000ull);
  CHECK(doc->Generation() == generation);
  CHECK(doc->value == 5);
  CHECK(doc->journal.size() == 2);
  CHECK(doc->journal[0].retained_since_us == 40ull * 1'000'000ull);
  CHECK(doc->journal[1].retained_since_us == 50ull * 1'000'000ull);

  // Exact boundary: retained_since = now - max_age must remain.
  ae::RamDomainStorage storage2;
  ae::Domain domain2{storage2};
  auto doc2 = MakeDoc(domain2);
  doc2->SetJournalRetentionPolicy(JournalRetentionPolicy{
      .max_events = 0,
      .max_age = std::chrono::seconds{25},
  });
  doc2->InsertLocalForTest(MakeAdd(domain2, 1), 1, 34ull * 1'000'000ull);
  doc2->InsertLocalForTest(MakeAdd(domain2, 1), 2, 35ull * 1'000'000ull);
  doc2->CompactJournal(60ull * 1'000'000ull);
  CHECK(doc2->journal.size() == 1);
  CHECK(doc2->journal[0].retained_since_us == 35ull * 1'000'000ull);
}

void TestCountAgeUnion() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto doc = MakeDoc(domain);
  // 20 records at t=1s..20s. now=20s, max_age=6s keeps last 7 (t>=14s).
  doc->SetJournalRetentionPolicy(JournalRetentionPolicy{
      .max_events = 3,
      .max_age = std::chrono::seconds{6},
  });
  for (int i = 0; i < 20; ++i) {
    doc->InsertLocalForTest(MakeAdd(domain, 1), static_cast<std::uint64_t>(i + 1),
                            static_cast<std::uint64_t>(i + 1) * 1'000'000ull);
  }
  doc->CompactJournal(20ull * 1'000'000ull);
  CHECK(doc->journal.size() == 7);
  CHECK(doc->value == 20);

  ae::RamDomainStorage storage2;
  ae::Domain domain2{storage2};
  auto doc2 = MakeDoc(domain2);
  doc2->SetJournalRetentionPolicy(JournalRetentionPolicy{
      .max_events = 10,
      .max_age = std::chrono::seconds{3},
  });
  for (int i = 0; i < 20; ++i) {
    doc2->InsertLocalForTest(
        MakeAdd(domain2, 1), static_cast<std::uint64_t>(i + 1),
        static_cast<std::uint64_t>(i + 1) * 1'000'000ull);
  }
  doc2->CompactJournal(20ull * 1'000'000ull);
  // Age keeps last 4; count keeps last 10; union = 10.
  CHECK(doc2->journal.size() == 10);
}

void TestCompactionBlocked() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto doc = MakeDoc(domain);
  doc->SetJournalRetentionPolicy(JournalRetentionPolicy{.max_events = 0});
  for (int i = 0; i < 100; ++i) {
    doc->Commit(MakeAdd(domain, 1));
  }
  auto const generation = doc->Generation();
  doc->SetJournalCompactionBlocked(true);
  doc->CompactJournal(SystemUtcMicros());
  CHECK(doc->journal.size() == 100);
  CHECK(doc->Generation() == generation);
  CHECK(doc->value == 100);

  doc->SetJournalCompactionBlocked(false);
  doc->CompactJournal(SystemUtcMicros());
  CHECK(doc->journal.size() == 0);
  CHECK(doc->Generation() == generation);
  CHECK(doc->value == 100);
  CHECK(CountReachableAddEvents(*doc) == 0);
}

void TestDynamicPolicy() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto doc = MakeDoc(domain);
  doc->SetJournalRetentionPolicy(JournalRetentionPolicy{.max_events = 100});
  for (int i = 0; i < 50; ++i) {
    doc->Commit(MakeAdd(domain, 1));
  }
  doc->CompactJournal(SystemUtcMicros());
  CHECK(doc->journal.size() == 50);

  doc->SetJournalRetentionPolicy(JournalRetentionPolicy{.max_events = 10});
  doc->CompactJournal(SystemUtcMicros());
  CHECK(doc->journal.size() == 10);

  doc->SetJournalRetentionPolicy(JournalRetentionPolicy{.max_events = 0});
  doc->SetJournalCompactionBlocked(true);
  doc->CompactJournal(SystemUtcMicros());
  CHECK(doc->journal.size() == 10);

  doc->SetJournalCompactionBlocked(false);
  doc->CompactJournal(SystemUtcMicros());
  CHECK(doc->journal.size() == 0);
  CHECK(doc->value == 50);
}

void TestSharedOrderPreserved() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto doc = MakeDoc(domain);
  doc->SetJournalRetentionPolicy(JournalRetentionPolicy{.max_events = 2});

  for (int i = 0; i < 5; ++i) {
    SharedEventId id{.origin_uid = "peer-a",
                     .origin_sequence = static_cast<std::uint64_t>(i + 1)};
    SharedEventOrder order{.lamport = static_cast<std::uint64_t>((i + 1) * 10),
                           .origin_uid = id.origin_uid,
                           .origin_sequence = id.origin_sequence};
    doc->InsertSharedForTest(MakeAdd(domain, 1), id, order,
                             static_cast<std::uint64_t>(i + 1) * 1'000'000ull);
  }
  auto const id4 = doc->journal[3].identity;
  auto const order4 = doc->journal[3].order;
  auto const id5 = doc->journal[4].identity;
  auto const order5 = doc->journal[4].order;

  doc->CompactJournal(SystemUtcMicros());
  CHECK(doc->journal.size() == 2);
  CHECK(doc->journal[0].identity == id4);
  CHECK(doc->journal[0].order == order4);
  CHECK(doc->journal[1].identity == id5);
  CHECK(doc->journal[1].order == order5);
  CHECK(doc->value == 5);
}

void TestMidInsertWhileBlocked() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto doc = MakeDoc(domain);
  doc->SetJournalCompactionBlocked(true);
  doc->SetJournalRetentionPolicy(JournalRetentionPolicy{.max_events = 0});

  SharedEventId id1{.origin_uid = "a", .origin_sequence = 1};
  SharedEventOrder o1{.lamport = 10, .origin_uid = "a", .origin_sequence = 1};
  SharedEventId id3{.origin_uid = "a", .origin_sequence = 3};
  SharedEventOrder o3{.lamport = 30, .origin_uid = "a", .origin_sequence = 3};
  doc->InsertSharedForTest(MakeAdd(domain, 1), id1, o1, 1);
  doc->InsertSharedForTest(MakeAdd(domain, 10), id3, o3, 2);
  CHECK(doc->value == 11);

  SharedEventId id2{.origin_uid = "a", .origin_sequence = 2};
  SharedEventOrder o2{.lamport = 20, .origin_uid = "a", .origin_sequence = 2};
  doc->InsertSharedForTest(MakeAdd(domain, 100), id2, o2, 3);
  CHECK(doc->journal.size() == 3);
  CHECK(doc->journal[0].order.lamport == 10);
  CHECK(doc->journal[1].order.lamport == 20);
  CHECK(doc->journal[2].order.lamport == 30);
  CHECK(doc->value == 111);

  doc->CompactJournal(SystemUtcMicros());
  CHECK(doc->journal.size() == 3);
}

void TestNodeV1MigrationStampsRetainedSince() {
  ae::RamDomainStorage storage;
  ae::ObjId id{};
  {
    ae::Domain domain{storage};
    auto doc = LegacyRetentionDoc::ptr::Create(ae::CreateWith{domain});
    doc->value = 0;
    InitializeRuntimeNode(*doc);
    for (int i = 0; i < 5; ++i) {
      auto event = LegacyAddEvent::ptr::Create(ae::CreateWith{domain});
      event->delta = 1;
      doc->Commit(event);
    }
    CHECK(doc->value == 5);
    for (auto const& record : doc->journal) {
      CHECK(record.retained_since_us != 0);
    }
    id = doc.id();
    doc.Save();
  }

  {
    ae::Domain domain{storage};
    auto loaded = LegacyRetentionDoc::ptr::Declare(
        ae::CreateWith{domain}.with_id(id));
    loaded.Load();
    CHECK(loaded);
    CHECK(loaded->value == 5);
    CHECK(loaded->journal.size() == 5);
    auto const stamp = loaded->journal[0].retained_since_us;
    CHECK(stamp != 0);
    for (auto const& record : loaded->journal) {
      CHECK(record.retained_since_us == stamp);
    }

    loaded->SetJournalRetentionPolicy(JournalRetentionPolicy{.max_events = 0});
    auto const generation = loaded->Generation();
    loaded->CompactJournal(SystemUtcMicros());
    CHECK(loaded->journal.empty());
    CHECK(loaded->value == 5);
    CHECK(loaded->Generation() == generation);
    loaded.Save();
  }

  {
    ae::Domain domain{storage};
    auto again = LegacyRetentionDoc::ptr::Declare(
        ae::CreateWith{domain}.with_id(id));
    again.Load();
    CHECK(again->value == 5);
    CHECK(again->journal.empty());
  }
}

void TestUnreferencedEventDirsAreNotLoaded() {
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_journal_retention_orphans";
  std::filesystem::remove_all(dir);
  ae::ObjId id{};
  std::size_t files_after_compact = 0;
  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto doc = MakeDoc(domain);
    doc->SetJournalRetentionPolicy(JournalRetentionPolicy{.max_events = 0});
    for (int i = 0; i < 20; ++i) {
      doc->Commit(MakeAdd(domain, 1));
    }
    id = doc.id();
    doc->CompactJournal(SystemUtcMicros());
    CHECK(doc->journal.empty());
    CHECK(CountReachableAddEvents(*doc) == 0);
    doc.Save();
  }
  for (auto const& entry :
       std::filesystem::recursive_directory_iterator{dir}) {
    if (entry.is_regular_file()) {
      ++files_after_compact;
    }
  }
  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto loaded = RetentionDoc::ptr::Declare(
        ae::CreateWith{domain}.with_id(id));
    loaded.Load();
    CHECK(loaded->value == 20);
    CHECK(loaded->journal.empty());
    CHECK(CountReachableAddEvents(*loaded) == 0);
  }
  // DirectoryDomainStorage SaveRoot does not delete unreachable event
  // object dirs. Startup must still ignore them.
  CHECK(files_after_compact > 0);
  std::filesystem::remove_all(dir);
}

void TestZeroRetentionFiveHundred() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto doc = MakeDoc(domain);
  doc->SetJournalRetentionPolicy(JournalRetentionPolicy{.max_events = 0});
  for (int i = 0; i < 500; ++i) {
    doc->Commit(MakeAdd(domain, 1));
  }
  CHECK(doc->journal.size() == 500);
  auto const generation = doc->Generation();
  doc->CompactJournal(SystemUtcMicros());
  CHECK(doc->journal.size() == 0);
  CHECK(doc->value == 500);
  CHECK(doc->Generation() == generation);
  CHECK(CountReachableAddEvents(*doc) == 0);

  doc.Save();
  auto reloaded =
      RetentionDoc::ptr::Declare(ae::CreateWith{domain}.with_id(doc.id()));
  reloaded.Load();
  CHECK(reloaded->value == 500);
  CHECK(reloaded->journal.size() == 0);
}

void TestRetentionTenReachable() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto doc = MakeDoc(domain);
  doc->SetJournalRetentionPolicy(JournalRetentionPolicy{.max_events = 10});
  for (int i = 0; i < 50; ++i) {
    doc->Commit(MakeAdd(domain, 1));
  }
  doc->CompactJournal(SystemUtcMicros());
  CHECK(doc->journal.size() == 10);
  CHECK(CountReachableAddEvents(*doc) == 10);
  CHECK(doc->value == 50);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::TestMaxEventsTen();
  apptraverse::test::TestAgeRetentionInclusive();
  apptraverse::test::TestCountAgeUnion();
  apptraverse::test::TestCompactionBlocked();
  apptraverse::test::TestDynamicPolicy();
  apptraverse::test::TestSharedOrderPreserved();
  apptraverse::test::TestMidInsertWhileBlocked();
  apptraverse::test::TestNodeV1MigrationStampsRetainedSince();
  apptraverse::test::TestZeroRetentionFiveHundred();
  apptraverse::test::TestRetentionTenReachable();
  apptraverse::test::TestUnreferencedEventDirsAreNotLoaded();
  std::cout << "journal_retention_test OK\n";
  return 0;
}
