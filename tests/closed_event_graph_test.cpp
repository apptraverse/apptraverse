#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/event.h"
#include "apptraverse/event_for.h"
#include "apptraverse/node.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/shared_event_id.h"
#include "apptraverse/shared_event_order.h"
#include "apptraverse/shared_network_graph.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

class CountingStorage final : public ae::IDomainStorage {
 public:
  std::unique_ptr<ae::IDomainStorageWriter> Store(
      ae::DomainQuery const& query) override {
    ++store_count;
    return inner.Store(query);
  }

  ae::ClassList Enumerate(ae::ObjId const& obj_id) override {
    return inner.Enumerate(obj_id);
  }

  ae::DomainLoad Load(ae::DomainQuery const& query) override {
    return inner.Load(query);
  }

  void Remove(ae::ObjId const& obj_id) override { inner.Remove(obj_id); }
  void CleanUp() override { inner.CleanUp(); }

  ae::RamDomainStorage inner;
  std::size_t store_count{0};
};

// 1. Metadata : Obj
class Metadata : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::Metadata", Metadata, ae::Obj, 0)

 protected:
  Metadata() = default;

 public:
  explicit Metadata(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(tag), AE_MMBR(scalar_old_id), AE_MMBR(local_link))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, tag, scalar_old_id, local_link);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, tag, scalar_old_id, local_link);
  }

  std::string tag;
  std::uint32_t scalar_old_id{0};
  LocalPtr<Metadata> local_link;
};

// 2. Item : Node
class Item : public NodeFor<Item> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::Item", Item, Node, 0)

 protected:
  Item() = default;

 public:
  explicit Item(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name), AE_MMBR(scalar_old_id), AE_MMBR(metadata),
                    AE_MMBR(local_link))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    Node::Load(ae::Version<3>{}, dnv);
    dnv(name, scalar_old_id, metadata, local_link);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    Node::Save(ae::Version<3>{}, dnv);
    dnv(name, scalar_old_id, metadata, local_link);
  }

  std::string name;
  std::uint32_t scalar_old_id{0};
  SharedPtr<Metadata> metadata;
  LocalPtr<Metadata> local_link;
};

// 3. ContainerNode : Node
class ContainerNode;
class EventWithExternalRef;

// 4. AddItemEvent : EventFor<ContainerNode, AddItemEvent>
class AddItemEvent : public EventFor<ContainerNode, AddItemEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::AddItemEvent", AddItemEvent,
                           Event, 0)

 protected:
  AddItemEvent() = default;

 public:
  explicit AddItemEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(item), AE_MMBR(item_alias), AE_MMBR(metadata),
                    AE_MMBR(scalar_old_id), AE_MMBR(local_link))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, item, item_alias, metadata, scalar_old_id, local_link);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, item, item_alias, metadata, scalar_old_id, local_link);
  }

  SharedPtr<Item> item;
  SharedPtr<Item> item_alias;
  SharedPtr<Metadata> metadata;
  std::uint32_t scalar_old_id{0};
  LocalPtr<Metadata> local_link;
};

class ContainerNode : public NodeFor<ContainerNode> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::ContainerNode", ContainerNode,
                           Node, 0)

 protected:
  ContainerNode() = default;

 public:
  explicit ContainerNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(items))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    Node::Load(ae::Version<3>{}, dnv);
    dnv(items);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    Node::Save(ae::Version<3>{}, dnv);
    dnv(items);
  }

  void Apply(AddItemEvent const& event) {
    items.push_back(event.item);
    NoteMaterializedChange();
  }

  bool CanApply(AddItemEvent const&) const { return true; }

  void Apply(EventWithExternalRef const&);
  bool CanApply(EventWithExternalRef const&) const;

  std::vector<SharedPtr<Item>> items;
};

// 5. External object and event for boundary refusal tests
class ExternalObject : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::ExternalObject", ExternalObject,
                           ae::Obj, 0)

 protected:
  ExternalObject() = default;

 public:
  explicit ExternalObject(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(secret))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, secret);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, secret);
  }

  std::string secret;
};

class EventWithExternalRef
    : public EventFor<ContainerNode, EventWithExternalRef> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::EventWithExternalRef",
                           EventWithExternalRef, Event, 0)

 protected:
  EventWithExternalRef() = default;

 public:
  explicit EventWithExternalRef(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(external))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, external);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, external);
  }

  SharedPtr<ExternalObject> external;
};

inline void ContainerNode::Apply(EventWithExternalRef const&) {}
inline bool ContainerNode::CanApply(EventWithExternalRef const&) const {
  return true;
}

// Sentinel object for receiver collision testing
class SentinelObject : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::SentinelObject", SentinelObject,
                           ae::Obj, 0)

 protected:
  SentinelObject() = default;

 public:
  explicit SentinelObject(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(marker))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, marker);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, marker);
  }

  std::string marker{"sentinel_intact"};
};

APPTRAVERSE_REGISTER(Metadata);
APPTRAVERSE_REGISTER(Item);
APPTRAVERSE_REGISTER(ContainerNode);
APPTRAVERSE_REGISTER(AddItemEvent);
APPTRAVERSE_REGISTER(ExternalObject);
APPTRAVERSE_REGISTER(EventWithExternalRef);
APPTRAVERSE_REGISTER(SentinelObject);

// Helper to construct a complete source bundle:
// AddItemEvent references Item, Item alias (second ref to same Item),
// and Metadata (referenced by both AddItemEvent and Item).
// Item has initialized creation-time state and a base snapshot.
struct SourceBundle {
  ae::RamDomainStorage storage;
  std::unique_ptr<ae::Domain> domain;
  Metadata::ptr metadata;
  Item::ptr base_item;
  Item::ptr item;
  AddItemEvent::ptr event;
  EventGraphExportBoundary boundary;
};

SourceBundle CreateSourceBundle() {
  SourceBundle b;
  b.domain = std::make_unique<ae::Domain>(b.storage);

  b.metadata = Metadata::ptr::Create(ae::CreateWith{*b.domain});
  b.metadata->tag = "item_meta_tag_alpha";
  b.metadata->scalar_old_id = 99999;

  b.item = Item::ptr::Create(ae::CreateWith{*b.domain});
  b.item->name = "item_v1";
  b.item->metadata = b.metadata;
  b.item->scalar_old_id = b.metadata.id().id();
  InitializeRuntimeNode(*b.item);

  // b.item now has initialized creation-time state and a base snapshot!
  b.base_item = Item::ptr{b.item->base};
  b.base_item->name = "base_snapshot_v0";

  b.event = AddItemEvent::ptr::Create(ae::CreateWith{*b.domain});
  b.event->item = b.item;
  b.event->item_alias = b.item;  // exact same Item reference (alias!)
  b.event->metadata = b.metadata;
  b.event->scalar_old_id = b.item.id().id();

  b.boundary.Permit(b.event.id());
  b.boundary.Permit(b.item.id());
  b.boundary.Permit(b.base_item.id());
  b.boundary.Permit(b.metadata.id());

  // Save all objects into source storage
  b.metadata.Save();
  b.base_item.Save();
  b.item.Save();
  b.event.Save();

  return b;
}

// 1. Test Event/Item/Metadata import and alias preservation
void TestImportAndAliasPreservation() {
  auto source = CreateSourceBundle();

  std::vector<std::uint8_t> payload;
  bool const frozen = FreezeClosedEventGraphPayload(*source.event.cached(),
                                                    source.boundary, payload);
  CHECK(frozen);
  CHECK(!payload.empty());

  ae::RamDomainStorage parsed;
  ae::ObjId root_id;
  bool const parsed_ok =
      ParseClosedEventGraphPayload(payload, parsed, root_id);
  CHECK(parsed_ok);
  CHECK(root_id == source.event.id());

  ae::RamDomainStorage receiver_storage;
  ae::Domain receiver_domain{receiver_storage};
  std::set<ae::ObjId> reserved_ids;
  std::map<ae::ObjId, ae::ObjId> mapping;

  auto imported = ImportClosedEventGraph(parsed, root_id, receiver_domain,
                                         receiver_storage, reserved_ids,
                                         &mapping);
  CHECK(imported);

  auto imported_event = imported.as<AddItemEvent>();
  CHECK(imported_event != nullptr);

  // Remapped receiver-local ObjId
  CHECK(imported_event->obj_id != source.event.id());
  CHECK(imported_event->obj_id == mapping[source.event.id()]);

  // Alias preservation: event->item and event->item_alias point to SAME Item
  CHECK(imported_event->item.is_valid());
  CHECK(imported_event->item_alias.is_valid());
  CHECK(imported_event->item.id() == imported_event->item_alias.id());
  CHECK(&*imported_event->item == &*imported_event->item_alias);

  // Item was remapped to new ID
  CHECK(imported_event->item.id() != source.item.id());
  CHECK(imported_event->item.id() == mapping[source.item.id()]);
  CHECK(imported_event->item->name == "item_v1");

  // Metadata preservation and sharing: referenced by both event and item
  CHECK(imported_event->metadata.is_valid());
  CHECK(imported_event->item->metadata.is_valid());
  CHECK(imported_event->metadata.id() == imported_event->item->metadata.id());
  CHECK(&*imported_event->metadata == &*imported_event->item->metadata);
  CHECK(imported_event->metadata.id() != source.metadata.id());
  CHECK(imported_event->metadata.id() == mapping[source.metadata.id()]);
  CHECK(imported_event->metadata->tag == "item_meta_tag_alpha");

  // Exactly one mapping entry per included object: 4 objects total
  CHECK(mapping.size() == 4);
  CHECK(mapping.count(source.event.id()) == 1);
  CHECK(mapping.count(source.item.id()) == 1);
  CHECK(mapping.count(source.base_item.id()) == 1);
  CHECK(mapping.count(source.metadata.id()) == 1);
}

// 2. Test Node::base remapping
void TestNodeBaseRemapping() {
  auto source = CreateSourceBundle();

  std::vector<std::uint8_t> payload;
  CHECK(FreezeClosedEventGraphPayload(*source.event.cached(), source.boundary,
                                      payload));

  ae::RamDomainStorage parsed;
  ae::ObjId root_id;
  CHECK(ParseClosedEventGraphPayload(payload, parsed, root_id));

  ae::RamDomainStorage receiver_storage;
  ae::Domain receiver_domain{receiver_storage};
  std::set<ae::ObjId> reserved_ids;
  std::map<ae::ObjId, ae::ObjId> mapping;

  auto imported = ImportClosedEventGraph(parsed, root_id, receiver_domain,
                                         receiver_storage, reserved_ids,
                                         &mapping);
  CHECK(imported);

  auto imported_event = imported.as<AddItemEvent>();
  CHECK(imported_event->item.is_valid());
  auto const& imported_item = imported_event->item;

  // Node::base was correctly included and remapped
  CHECK(imported_item->base.is_valid());
  CHECK(imported_item->base.id() != source.base_item.id());
  CHECK(imported_item->base.id() == mapping[source.base_item.id()]);
  CHECK(&*imported_item->base != nullptr);
  auto base_item_casted = static_cast<Item*>(&*imported_item->base);
  CHECK(base_item_casted != nullptr);
  CHECK(base_item_casted->name == "base_snapshot_v0");
}

// 3. Test all sender IDs occupied on receiver by sentinel objects
void TestAllSenderIdsOccupiedBySentinels() {
  auto source = CreateSourceBundle();

  std::vector<std::uint8_t> payload;
  CHECK(FreezeClosedEventGraphPayload(*source.event.cached(), source.boundary,
                                      payload));

  ae::RamDomainStorage parsed;
  ae::ObjId root_id;
  CHECK(ParseClosedEventGraphPayload(payload, parsed, root_id));

  ae::RamDomainStorage receiver_storage;
  ae::Domain receiver_domain{receiver_storage};

  // Pre-occupy on receiver EVERY sender ObjId with a sentinel object
  std::vector<ae::ObjId> sender_ids{
      source.event.id(), source.item.id(),
      source.base_item.id(), source.metadata.id()};

  std::vector<SentinelObject::ptr> sentinels;
  sentinels.reserve(sender_ids.size());
  for (auto const& sender_id : sender_ids) {
    auto sentinel = SentinelObject::ptr::Create(
        ae::CreateWith{receiver_domain}.with_id(sender_id));
    sentinel->marker = "sentinel_" + std::to_string(sender_id.id());
    sentinel.Save();
    sentinels.push_back(std::move(sentinel));
  }

  std::set<ae::ObjId> reserved_ids;
  std::map<ae::ObjId, ae::ObjId> mapping;

  auto imported = ImportClosedEventGraph(parsed, root_id, receiver_domain,
                                         receiver_storage, reserved_ids,
                                         &mapping);
  CHECK(imported);

  // Every remapped ID must NOT collide with ANY sender / sentinel ID
  for (auto const& [old_id, new_id] : mapping) {
    for (auto const& sender_id : sender_ids) {
      CHECK(new_id != sender_id);
    }
  }

  // All sentinels remain completely intact and untouched
  for (auto const& sender_id : sender_ids) {
    auto obj = receiver_domain.Find(sender_id);
    CHECK(obj);
    auto sentinel = obj.as<SentinelObject>();
    CHECK(sentinel != nullptr);
    CHECK(sentinel->marker == "sentinel_" + std::to_string(sender_id.id()));
  }
}

// 4. Test zero source writes during freeze
void TestZeroSourceWritesDuringFreeze() {
  CountingStorage watched_storage;
  auto domain = std::make_unique<ae::Domain>(watched_storage);

  auto meta = Metadata::ptr::Create(ae::CreateWith{*domain});
  meta->tag = "zero_write_test";

  auto item = Item::ptr::Create(ae::CreateWith{*domain});
  item->name = "item_zero_write";
  item->metadata = meta;
  InitializeRuntimeNode(*item);

  auto event = AddItemEvent::ptr::Create(ae::CreateWith{*domain});
  event->item = item;
  event->item_alias = item;
  event->metadata = meta;

  meta.Save();
  item.Save();
  event.Save();

  // Reset store counter
  watched_storage.store_count = 0;

  EventGraphExportBoundary boundary{event.id(), item.id(), item->base.id(),
                                    meta.id()};
  std::vector<std::uint8_t> payload;
  bool const ok =
      FreezeClosedEventGraphPayload(*event.cached(), boundary, payload);
  CHECK(ok);
  CHECK(!payload.empty());

  // STRICT ZERO WRITES TO SOURCE STORAGE
  CHECK(watched_storage.store_count == 0);

  // Source objects unchanged
  CHECK(meta->tag == "zero_write_test");
  CHECK(item->name == "item_zero_write");
}

// 5. Test source destroyed before import
void TestSourceDestroyedBeforeImport() {
  std::vector<std::uint8_t> payload;
  ae::ObjId expected_root_id;

  {
    auto source = CreateSourceBundle();
    expected_root_id = source.event.id();
    CHECK(FreezeClosedEventGraphPayload(*source.event.cached(), source.boundary,
                                        payload));
    // source scope ends here: source Domain and source storage are destroyed!
  }

  // Receiver operates with ZERO dependency on source domain/storage
  ae::RamDomainStorage parsed;
  ae::ObjId root_id;
  CHECK(ParseClosedEventGraphPayload(payload, parsed, root_id));
  CHECK(root_id == expected_root_id);

  ae::RamDomainStorage receiver_storage;
  ae::Domain receiver_domain{receiver_storage};
  std::set<ae::ObjId> reserved_ids;

  auto imported = ImportClosedEventGraph(parsed, root_id, receiver_domain,
                                         receiver_storage, reserved_ids);
  CHECK(imported);

  auto imported_event = imported.as<AddItemEvent>();
  CHECK(imported_event != nullptr);
  CHECK(imported_event->item.is_valid());
  CHECK(imported_event->item->name == "item_v1");
  CHECK(imported_event->item->metadata.is_valid());
  CHECK(imported_event->item->metadata->tag == "item_meta_tag_alpha");
}

// 6. Test LocalPtr exclusion
void TestLocalPtrExclusion() {
  auto source = CreateSourceBundle();

  // Create a local metadata object that should NOT be exported
  auto local_meta = Metadata::ptr::Create(ae::CreateWith{*source.domain});
  local_meta->tag = "local_only_secret";

  source.event->local_link = local_meta;
  source.item->local_link = local_meta;

  std::vector<std::uint8_t> payload;
  CHECK(FreezeClosedEventGraphPayload(*source.event.cached(), source.boundary,
                                      payload));

  ae::RamDomainStorage parsed;
  ae::ObjId root_id;
  CHECK(ParseClosedEventGraphPayload(payload, parsed, root_id));

  // The local_meta object was NOT exported into parsed storage
  CHECK(parsed.state.find(local_meta.id()) == parsed.state.end());

  ae::RamDomainStorage receiver_storage;
  ae::Domain receiver_domain{receiver_storage};
  std::set<ae::ObjId> reserved_ids;

  auto imported = ImportClosedEventGraph(parsed, root_id, receiver_domain,
                                         receiver_storage, reserved_ids);
  CHECK(imported);

  auto imported_event = imported.as<AddItemEvent>();
  CHECK(imported_event != nullptr);

  // LocalPtr references are excluded and left empty
  CHECK(!imported_event->local_link.is_valid());
  CHECK(!imported_event->item->local_link.is_valid());
}

// 7. Test explicit external-reference refusal
void TestExplicitExternalReferenceRefusal() {
  auto source = CreateSourceBundle();

  // Add an external object reachable from event
  auto external = ExternalObject::ptr::Create(ae::CreateWith{*source.domain});
  external->secret = "unpermitted_foreign_object";

  auto event_with_ext =
      EventWithExternalRef::ptr::Create(ae::CreateWith{*source.domain});
  event_with_ext->external = external;

  external.Save();
  event_with_ext.Save();

  // Boundary permits only the event, NOT the external object
  EventGraphExportBoundary boundary{event_with_ext.id()};
  std::vector<std::uint8_t> payload;

  // MUST BE REFUSED EXPLICITLY
  bool const frozen = FreezeClosedEventGraphPayload(
      *event_with_ext.cached(), boundary, payload);
  CHECK(!frozen);
  CHECK(payload.empty());
}

// 8. Test malformed/missing-reference rejection without receiver writes
void TestMalformedMissingReferenceRejectionWithoutReceiverWrites() {
  auto source = CreateSourceBundle();

  std::vector<std::uint8_t> payload;
  CHECK(FreezeClosedEventGraphPayload(*source.event.cached(), source.boundary,
                                      payload));

  ae::RamDomainStorage parsed;
  ae::ObjId root_id;
  CHECK(ParseClosedEventGraphPayload(payload, parsed, root_id));

  // Corrupt parsed storage: remove metadata so Item has a missing reference
  parsed.state.erase(source.metadata.id());

  // Verification in scratch must fail
  CHECK(!ValidateClosedEventGraphStorage(parsed, root_id));

  // Import to receiver with CountingStorage
  CountingStorage receiver_storage;
  ae::Domain receiver_domain{receiver_storage};
  std::set<ae::ObjId> reserved_ids;

  auto imported = ImportClosedEventGraph(parsed, root_id, receiver_domain,
                                         receiver_storage, reserved_ids);
  CHECK(!imported);

  // ZERO WRITES TO RECEIVER STORAGE
  CHECK(receiver_storage.store_count == 0);

  // Receiver domain remains clean
  CHECK(!receiver_domain.Find(root_id));
}

// 9. Test receiver Save → destroy Domain → Load
void TestReceiverSaveDestroyDomainLoad() {
  auto source = CreateSourceBundle();

  std::vector<std::uint8_t> payload;
  CHECK(FreezeClosedEventGraphPayload(*source.event.cached(), source.boundary,
                                      payload));

  ae::RamDomainStorage parsed;
  ae::ObjId root_id;
  CHECK(ParseClosedEventGraphPayload(payload, parsed, root_id));

  ae::RamDomainStorage receiver_storage;
  ae::ObjId imported_event_id;

  {
    ae::Domain receiver_domain{receiver_storage};
    std::set<ae::ObjId> reserved_ids;
    std::map<ae::ObjId, ae::ObjId> mapping;

    auto imported = ImportClosedEventGraph(parsed, root_id, receiver_domain,
                                           receiver_storage, reserved_ids,
                                           &mapping);
    CHECK(imported);
    imported_event_id = mapping[root_id];
    // receiver_domain is destroyed here!
  }

  // Create brand new domain from receiver_storage and load the imported event
  ae::Domain reloaded_domain{receiver_storage};
  ae::DomainGraph load_graph{&reloaded_domain,
                             ae::GraphSerializationScope::NetworkShared};

  auto loaded_root = load_graph.LoadRoot(imported_event_id);
  CHECK(loaded_root);

  auto loaded_event = loaded_root.as<AddItemEvent>();
  CHECK(loaded_event != nullptr);
  CHECK(loaded_event->item.is_valid());
  CHECK(loaded_event->item_alias.is_valid());
  CHECK(loaded_event->item.id() == loaded_event->item_alias.id());
  CHECK(loaded_event->item->name == "item_v1");
  CHECK(loaded_event->item->metadata.is_valid());
  CHECK(loaded_event->item->metadata->tag == "item_meta_tag_alpha");
  CHECK(loaded_event->metadata.is_valid());
  CHECK(loaded_event->metadata.id() == loaded_event->item->metadata.id());
}

// 10. Test AddItem application and forced replay preserve the same local Item
void TestAddItemApplicationAndForcedReplay() {
  auto source = CreateSourceBundle();

  std::vector<std::uint8_t> payload;
  CHECK(FreezeClosedEventGraphPayload(*source.event.cached(), source.boundary,
                                      payload));

  ae::RamDomainStorage parsed;
  ae::ObjId root_id;
  CHECK(ParseClosedEventGraphPayload(payload, parsed, root_id));

  ae::RamDomainStorage receiver_storage;
  ae::Domain receiver_domain{receiver_storage};
  std::set<ae::ObjId> reserved_ids;

  auto imported = ImportClosedEventGraph(parsed, root_id, receiver_domain,
                                         receiver_storage, reserved_ids);
  CHECK(imported);

  auto imported_event = imported.as<AddItemEvent>();
  CHECK(imported_event != nullptr);

  // Setup receiver ContainerNode
  auto container = ContainerNode::ptr::Create(ae::CreateWith{receiver_domain});
  InitializeRuntimeNode(*container);

  // Apply AddItemEvent
  container->Apply(*imported_event);
  CHECK(container->items.size() == 1);
  auto const local_item_id = container->items[0].id();
  auto const* local_item_raw_ptr = &*container->items[0];
  CHECK(local_item_id == imported_event->item.id());
  CHECK(local_item_raw_ptr == &*imported_event->item);

  // Commit to journal for replay
  Event::ptr const event_ptr{&receiver_domain, imported->obj_id,
                             ae::ObjFlags::kNone, imported};
  container->CommitShared(
      event_ptr, SharedEventId{"test-origin", 1},
      SharedEventOrder{100});
  CHECK(container->journal.size() == 1);

  // Forced replay from base
  container->ReplayFromBase();

  // Exactly the same local Item instance: not re-created!
  CHECK(container->items.size() == 1);
  CHECK(container->items[0].id() == local_item_id);
  CHECK(&*container->items[0] == local_item_raw_ptr);
  CHECK(container->items[0]->name == "item_v1");
}

// 11. Test scalar value equal to an old object ID is not remapped
void TestScalarValueEqualToOldObjectIdNotRemapped() {
  auto source = CreateSourceBundle();

  // Set scalar fields to exactly equal the old ObjId numbers
  auto const old_item_id = source.item.id().id();
  auto const old_meta_id = source.metadata.id().id();
  source.metadata->scalar_old_id = old_item_id;
  source.item->scalar_old_id = old_meta_id;
  source.event->scalar_old_id = old_item_id;

  std::vector<std::uint8_t> payload;
  CHECK(FreezeClosedEventGraphPayload(*source.event.cached(), source.boundary,
                                      payload));

  ae::RamDomainStorage parsed;
  ae::ObjId root_id;
  CHECK(ParseClosedEventGraphPayload(payload, parsed, root_id));

  ae::RamDomainStorage receiver_storage;
  ae::Domain receiver_domain{receiver_storage};
  std::set<ae::ObjId> reserved_ids;
  std::map<ae::ObjId, ae::ObjId> mapping;

  auto imported = ImportClosedEventGraph(parsed, root_id, receiver_domain,
                                         receiver_storage, reserved_ids,
                                         &mapping);
  CHECK(imported);

  auto imported_event = imported.as<AddItemEvent>();
  CHECK(imported_event != nullptr);

  // Object IDs were remapped to NEW distinct values
  CHECK(imported_event->item.id().id() != old_item_id);
  CHECK(imported_event->metadata.id().id() != old_meta_id);

  // Scalar values equal to old object IDs remain UNCHANGED
  CHECK(imported_event->scalar_old_id == old_item_id);
  CHECK(imported_event->item->scalar_old_id == old_meta_id);
  CHECK(imported_event->metadata->scalar_old_id == old_item_id);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();

  std::cout << "Running closed event graph tests...\n";
  apptraverse::test::TestImportAndAliasPreservation();
  apptraverse::test::TestNodeBaseRemapping();
  apptraverse::test::TestAllSenderIdsOccupiedBySentinels();
  apptraverse::test::TestZeroSourceWritesDuringFreeze();
  apptraverse::test::TestSourceDestroyedBeforeImport();
  apptraverse::test::TestLocalPtrExclusion();
  apptraverse::test::TestExplicitExternalReferenceRefusal();
  apptraverse::test::TestMalformedMissingReferenceRejectionWithoutReceiverWrites();
  apptraverse::test::TestReceiverSaveDestroyDomainLoad();
  apptraverse::test::TestAddItemApplicationAndForcedReplay();
  apptraverse::test::TestScalarValueEqualToOldObjectIdNotRemapped();

  std::cout << "All closed event graph tests passed successfully!\n";
  return 0;
}
