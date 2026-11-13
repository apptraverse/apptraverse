#include <cstdlib>
#include <iostream>
#include <string>

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/shared_graph.h"

namespace apptraverse::test {
namespace {

#define CHECK(condition)                                                    \
  do {                                                                      \
    if (!(condition)) {                                                      \
      std::cerr << "CHECK failed: " #condition << " at " << __LINE__ << '\n'; \
      std::exit(1);                                                         \
    }                                                                       \
  } while (false)

class ImportValue : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::ImportValue", ImportValue,
                           ae::Obj, 0)
 protected:
  ImportValue() = default;
 public:
  explicit ImportValue(ae::ObjProp prop) : Obj{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(text))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, text); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, text); }
  std::string text;
};

class ImportForeign : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::ImportForeign", ImportForeign,
                           ae::Obj, 0)
 protected:
  ImportForeign() = default;
 public:
  explicit ImportForeign(ae::ObjProp prop) : Obj{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(text))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, text); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_, text); }
  std::string text;
};

class ImportEvent;
class ImportTarget : public NodeFor<ImportTarget> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::ImportTarget", ImportTarget,
                           Node, 0)
 protected:
  ImportTarget() = default;
 public:
  explicit ImportTarget(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT()
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_); }
  void Apply(ImportEvent const&) {}
};

class ImportEvent : public EventFor<ImportTarget, ImportEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::ImportEvent", ImportEvent,
                           Event, 0)
 protected:
  ImportEvent() = default;
 public:
  explicit ImportEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(label), AE_MMBR(value), AE_MMBR(alias), AE_MMBR(local))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_, label, value, alias, local); }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, label, value, alias, local);
  }
  std::string label;
  SharedPtr<ImportValue> value;
  SharedPtr<ImportValue> alias;
  LocalPtr<ImportValue> local;
};

APPTRAVERSE_REGISTER(ImportValue);
APPTRAVERSE_REGISTER(ImportForeign);
APPTRAVERSE_REGISTER(ImportTarget);
APPTRAVERSE_REGISTER(ImportEvent);

struct Fixture {
  SharedGraphSnapshot snapshot;
  ae::ObjId value_id;
  ae::ObjId private_id;
};

Fixture FrozenEvent() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto value = ImportValue::ptr::Create(ae::CreateWith{domain});
  value->text = "sender old value";
  auto local = ImportValue::ptr::Create(ae::CreateWith{domain});
  local->text = "sender private";
  auto event = ImportEvent::ptr::Create(ae::CreateWith{domain});
  event->label = "original Event";
  event->value = value;
  event->alias = value;
  event->local = local;
  Fixture fixture{{}, value.id(), local.id()};
  bool const captured = CaptureSharedGraph(*event, fixture.snapshot);
  CHECK(captured);
  CHECK(fixture.snapshot.layers.count(local.id()) == 0);
  return fixture;  // Sender Domain/storage are gone before any import.
}

void TestNewAliasesAndKnownLoadedObjects() {
  auto fixture = FrozenEvent();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  ae::Ptr<ae::Obj> first;
  bool imported = ImportSharedGraphNewObjects(fixture.snapshot, domain, storage, first);
  CHECK(imported);
  auto* event = first.as<ImportEvent>();
  CHECK(event && event->label == "original Event");
  CHECK(event->value->text == "sender old value");
  CHECK(event->value.id() == fixture.value_id);
  CHECK(&*event->value == &*event->alias);
  CHECK(!event->local.is_valid());
  CHECK(storage.state.count(fixture.private_id) == 0);
  auto* const value_address = &*event->value;
  // Establish newer receiver state; importing stale dependency layers must not
  // hydrate these known objects or replace their persisted native bytes.
  event->value->text = "receiver newer value";
  event->label = "receiver known Event";
  ae::DomainGraph save{&domain};
  auto const saved = save.SaveRoot(first, first->obj_id);
  CHECK(saved.IsOk());
  auto const persisted = storage.state;
  ae::Ptr<ae::Obj> repeated;
  imported = ImportSharedGraphNewObjects(fixture.snapshot, domain, storage, repeated);
  CHECK(imported && repeated.get() == first.get());
  CHECK(&*event->value == value_address);
  CHECK(event->value->text == "receiver newer value");
  CHECK(event->label == "receiver known Event");
  CHECK(storage.state == persisted);
}

void TestKnownColdDependency() {
  auto fixture = FrozenEvent();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  {
    auto value = ImportValue::ptr::Create(
        ae::CreateWith{domain}.with_id(fixture.value_id));
    value->text = "cold receiver value";
    value.Save();
  }
  CHECK(!domain.Find(fixture.value_id));
  auto const cold_layers = storage.state.at(fixture.value_id);
  ae::Ptr<ae::Obj> imported;
  bool const accepted = ImportSharedGraphNewObjects(
      fixture.snapshot, domain, storage, imported);
  CHECK(accepted);
  auto* event = imported.as<ImportEvent>();
  CHECK(event && event->value->text == "cold receiver value");
  CHECK(&*event->value == &*event->alias);
  CHECK(storage.state.at(fixture.value_id) == cold_layers);
}

void TestClassCollisionRejectsBeforeAnyWrite(bool cold) {
  auto fixture = FrozenEvent();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto foreign = ImportForeign::ptr::Create(
      ae::CreateWith{domain}.with_id(fixture.value_id));
  foreign->text = "unrelated local object";
  if (cold) {
    foreign.Save();
    foreign = ImportForeign::ptr{};
    CHECK(!domain.Find(fixture.value_id));
  }
  auto const before = storage.state;
  ae::Ptr<ae::Obj> imported;
  bool const accepted = ImportSharedGraphNewObjects(
      fixture.snapshot, domain, storage, imported);
  CHECK(!accepted && !imported);
  CHECK(storage.state == before);
  CHECK(!domain.Find(fixture.snapshot.root_id));
  CHECK(storage.Enumerate(fixture.snapshot.root_id).empty());
  if (!cold) { CHECK(foreign->text == "unrelated local object"); }
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureCoreObjectRegistration();
  apptraverse::test::TestNewAliasesAndKnownLoadedObjects();
  apptraverse::test::TestKnownColdDependency();
  apptraverse::test::TestClassCollisionRejectsBeforeAnyWrite(false);
  apptraverse::test::TestClassCollisionRejectsBeforeAnyWrite(true);
  std::cout << "shared_graph_import_test OK\n";
}
