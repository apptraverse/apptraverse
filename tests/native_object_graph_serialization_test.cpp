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

#include "aether-miscpp/serialization/binary_archive.h"
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

class NativeSentinel : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::NativeSentinel", NativeSentinel,
                           ae::Obj, 0)
 protected:
  NativeSentinel() = default;

 public:
  explicit NativeSentinel(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(info))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, info);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, info);
  }

  std::string info;
};

class NativeTestDetail : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::NativeTestDetail",
                           NativeTestDetail, ae::Obj, 0)
 protected:
  NativeTestDetail() = default;

 public:
  explicit NativeTestDetail(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(info))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, info);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, info);
  }

  std::string info;
};

class NativeTestMetadata : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::NativeTestMetadata",
                           NativeTestMetadata, ae::Obj, 0)
 protected:
  NativeTestMetadata() = default;

 public:
  explicit NativeTestMetadata(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(tag), AE_MMBR(local_link), AE_MMBR(detail),
                    AE_MMBR(scalar_obj_id))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, tag, local_link, detail, scalar_obj_id);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, tag, local_link, detail, scalar_obj_id);
  }

  std::string tag;
  LocalPtr<NativeTestMetadata> local_link;
  SharedPtr<NativeTestDetail> detail;
  ae::ObjId scalar_obj_id;
};

struct NativeNestedRefStruct {
  SharedPtr<NativeTestDetail> detail;
  std::string note;

  AE_REFLECT(AE_MMBR(detail), AE_MMBR(note))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(detail, note);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(detail, note);
  }
};

class NativeTestEvent;

class NativeTestItem : public NodeFor<NativeTestItem, Node> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::NativeTestItem", NativeTestItem,
                           Node, 0)
 protected:
  NativeTestItem() = default;

 public:
  explicit NativeTestItem(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name), AE_MMBR(metadata))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, name, metadata);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, name, metadata);
  }

  void Apply(NativeTestEvent const& event);

  std::string name;
  SharedPtr<NativeTestMetadata> metadata;
};

class NativeTestEvent : public EventFor<NativeTestItem, NativeTestEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::NativeTestEvent",
                           NativeTestEvent, Event, 0)
 protected:
  NativeTestEvent() = default;

 public:
  explicit NativeTestEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_REF_BASE(Event), AE_MMBR(item),
                    AE_MMBR(second_item_ref), AE_MMBR(metadata),
                    AE_MMBR(second_metadata_ref), AE_MMBR(nested_struct),
                    AE_MMBR(nested_vector), AE_MMBR(event_detail),
                    AE_MMBR(scalar_event_id))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, item, second_item_ref, metadata, second_metadata_ref,
        nested_struct, nested_vector, event_detail, scalar_event_id);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, item, second_item_ref, metadata, second_metadata_ref,
        nested_struct, nested_vector, event_detail, scalar_event_id);
  }

  SharedPtr<NativeTestItem> item;
  SharedPtr<NativeTestItem> second_item_ref;
  SharedPtr<NativeTestMetadata> metadata;
  SharedPtr<NativeTestMetadata> second_metadata_ref;
  NativeNestedRefStruct nested_struct;
  std::vector<NativeNestedRefStruct> nested_vector;
  SharedPtr<NativeTestDetail> event_detail;
  ae::ObjId scalar_event_id;
};

APPTRAVERSE_REGISTER(NativeSentinel);
APPTRAVERSE_REGISTER(NativeTestDetail);
APPTRAVERSE_REGISTER(NativeTestMetadata);
APPTRAVERSE_REGISTER(NativeTestItem);
APPTRAVERSE_REGISTER(NativeTestEvent);

void NativeTestItem::Apply(NativeTestEvent const& event) {
  (void)event;
}

void TestNativeObjectGraphSerializationProof() {
  std::set<ae::ObjId> sender_ids;

  std::vector<std::uint8_t> payload;
  ae::ObjId event_id;

  // 1. SETUP SOURCE GRAPH
  {
    ae::RamDomainStorage source_storage;
    auto source_domain = std::make_unique<ae::Domain>(source_storage);

    auto detail = NativeTestDetail::ptr::Create(ae::CreateWith{*source_domain});
    detail->info = "detail_info_v0";

    auto metadata =
        NativeTestMetadata::ptr::Create(ae::CreateWith{*source_domain});
    metadata->tag = "meta_tag";
    metadata->detail = detail;
    metadata->scalar_obj_id = ae::ObjId{999};

    auto local_meta =
        NativeTestMetadata::ptr::Create(ae::CreateWith{*source_domain});
    local_meta->tag = "local_only";
    metadata->local_link = local_meta;

    auto item = NativeTestItem::ptr::Create(ae::CreateWith{*source_domain});
    item->name = "native_item";
    item->metadata = metadata;
    InitializeRuntimeNode(*item);

    auto event = NativeTestEvent::ptr::Create(ae::CreateWith{*source_domain});
    event->item = item;
    event->second_item_ref = item;  // Alias to same Item
    event->metadata = metadata;
    event->second_metadata_ref = metadata;  // Alias to same Metadata
    event->nested_struct.detail = detail;
    event->nested_struct.note = "nested_note";
    event->nested_vector.push_back({detail, "vec_note_0"});
    event->event_detail = detail;  // Alias to same Detail
    event->scalar_event_id = ae::ObjId{888};

    detail.Save();
    metadata.Save();
    local_meta.Save();
    item->base.Save();
    item.Save();
    event.Save();

    event_id = event.id();
    sender_ids.insert(detail.id());
    sender_ids.insert(metadata.id());
    sender_ids.insert(local_meta.id());
    sender_ids.insert(item.id());
    sender_ids.insert(item->base.id());
    sender_ids.insert(event.id());

    // 2. SERIALIZE USING ONLY NATIVE AETHER SERIALIZATION
    ae::RamDomainStorage scratch;
    {
      ae::Domain scratch_domain{scratch};
      ae::DomainGraph graph{&scratch_domain,
                            ae::GraphSerializationScope::NetworkShared};
      auto root_ptr = source_domain->Find(event.id());
      CHECK(root_ptr);
      graph.SaveRoot(root_ptr, event.id());
    }

    // Direct native BinaryArchive serialization of scratch state
    ae::seri::BinaryVectorBuffer buffer{payload};
    ae::seri::BinaryArchive archive{buffer};
    CHECK(archive.Save(event_id));
    CHECK(archive.Save(scratch.state));

    // 3. DESTROY SOURCE DOMAIN & STORAGE
    event.Reset();
    item.Reset();
    metadata.Reset();
    local_meta.Reset();
    detail.Reset();

    source_domain.reset();
  }

  // 4. DESERIALIZE INTO INDEPENDENT RECEIVER DOMAIN

  // 4. DESERIALIZE INTO INDEPENDENT RECEIVER DOMAIN
  {
    // Native BinaryArchive deserialization
    ae::seri::BinaryVectorBuffer read_buffer{payload};
    ae::seri::BinaryArchive read_archive{read_buffer};
    ae::ObjId wire_root_id;
    ae::RamDomainStorage parsed_storage;
    CHECK(read_archive.Load(wire_root_id));
    CHECK(read_archive.Load(parsed_storage.state));
    CHECK(wire_root_id == event_id);

    // Setup receiver storage with sentinels occupying all sender IDs
    ae::RamDomainStorage receiver_storage;
    auto receiver_domain = std::make_unique<ae::Domain>(receiver_storage);
    std::vector<NativeSentinel::ptr> sentinels;
    for (auto const& sid : sender_ids) {
      auto sentinel = NativeSentinel::ptr::Create(
          ae::CreateWith{*receiver_domain}.with_id(sid));
      sentinel->info = "occupied";
      sentinel.Save();
      sentinels.push_back(sentinel);
    }

    // Validate and Import into receiver
    CHECK(ValidateClosedEventGraphStorage(parsed_storage, wire_root_id));

    std::set<ae::ObjId> reserved_ids;
    auto imported_root =
        ImportClosedEventGraph(parsed_storage, wire_root_id, *receiver_domain,
                               receiver_storage, reserved_ids);
    CHECK(imported_root);

    auto imported_event = imported_root.as<NativeTestEvent>();
    CHECK(imported_event);

    // Verify dynamic class
    CHECK(imported_event->GetClassId() == NativeTestEvent::kClassId);

    // Verify receiver-local IDs are completely disjoint from sender IDs
    CHECK(sender_ids.find(imported_event->obj_id) == sender_ids.end());
    CHECK(sender_ids.find(imported_event->item.id()) == sender_ids.end());
    CHECK(sender_ids.find(imported_event->metadata.id()) == sender_ids.end());
    CHECK(sender_ids.find(imported_event->event_detail.id()) ==
          sender_ids.end());

    // Verify Sentinels are untouched
    for (auto const& sentinel : sentinels) {
      CHECK(sentinel->info == "occupied");
    }

    // Verify Aliases preserved
    CHECK(imported_event->item.id() == imported_event->second_item_ref.id());
    CHECK(imported_event->item == imported_event->second_item_ref);
    CHECK(&*imported_event->item == &*imported_event->second_item_ref);

    CHECK(imported_event->metadata.id() ==
          imported_event->second_metadata_ref.id());
    CHECK(imported_event->metadata == imported_event->second_metadata_ref);
    CHECK(&*imported_event->metadata == &*imported_event->second_metadata_ref);

    CHECK(imported_event->event_detail.id() ==
          imported_event->metadata->detail.id());
    CHECK(imported_event->event_detail.id() ==
          imported_event->nested_struct.detail.id());
    CHECK(imported_event->nested_vector.size() == 1);
    CHECK(imported_event->event_detail.id() ==
          imported_event->nested_vector[0].detail.id());
    CHECK(&*imported_event->event_detail ==
          &*imported_event->metadata->detail);

    // Verify Node::base is valid and remapped
    CHECK(imported_event->item->base.is_valid());
    CHECK(sender_ids.find(imported_event->item->base.id()) == sender_ids.end());
    CHECK(imported_event->item->base.domain() == receiver_domain.get());

    // Verify LocalPtr is absent (null/empty)
    CHECK(!imported_event->metadata->local_link.is_valid());

    // Verify scalar ObjId-looking values unchanged
    CHECK(imported_event->metadata->scalar_obj_id == ae::ObjId{999});
    CHECK(imported_event->scalar_event_id == ae::ObjId{888});

    // 5. RECEIVER SAVE -> DESTROY DOMAIN -> LOAD RESTART PROOF
    auto const saved_event_id = imported_event->obj_id;

    // Clear live references before destroying receiver_domain
    imported_event = nullptr;
    imported_root.Reset();
    sentinels.clear();

    ae::RamDomainStorage reload_storage = receiver_storage;
    receiver_domain.reset();

    // Reload from receiver_storage in a fresh domain
    ae::Domain reload_domain{reload_storage};
    ae::DomainGraph reload_graph{&reload_domain,
                                 ae::GraphSerializationScope::NetworkShared};
    auto reloaded_root = reload_graph.LoadRoot(saved_event_id);
    CHECK(reloaded_root);

    auto reloaded_event = reloaded_root.as<NativeTestEvent>();
    CHECK(reloaded_event);

    // Verify all aliases and values after reload
    CHECK(reloaded_event->item.id() == reloaded_event->second_item_ref.id());
    CHECK(reloaded_event->item == reloaded_event->second_item_ref);
    CHECK(&*reloaded_event->item == &*reloaded_event->second_item_ref);
    CHECK(reloaded_event->metadata.id() ==
          reloaded_event->second_metadata_ref.id());
    CHECK(reloaded_event->metadata == reloaded_event->second_metadata_ref);
    CHECK(&*reloaded_event->metadata == &*reloaded_event->second_metadata_ref);
    CHECK(reloaded_event->event_detail.id() ==
          reloaded_event->metadata->detail.id());
    CHECK(&*reloaded_event->event_detail ==
          &*reloaded_event->metadata->detail);
    CHECK(reloaded_event->item->base.is_valid());
    CHECK(!reloaded_event->metadata->local_link.is_valid());
    CHECK(reloaded_event->metadata->scalar_obj_id == ae::ObjId{999});
    CHECK(reloaded_event->scalar_event_id == ae::ObjId{888});
  }
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  std::cout << "Running native object graph serialization proof test...\n";
  apptraverse::test::TestNativeObjectGraphSerializationProof();
  std::cout << "PASS: Native object graph serialization proof test passed!\n";
  return 0;
}
