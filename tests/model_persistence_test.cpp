#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/journal_retention_policy.h"
#include "apptraverse/model_persistence.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/partitioned_model_domain_storage.h"
#include "apptraverse/runtime_lifecycle.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse::test {
namespace {

class DocNode;
class DocChildNode;
class DocChangedEvent;

class DocChildNode : public NodeFor<DocChildNode> {
  APPTRAVERSE_OBJECT(DocChildNode, Node, 0)

 protected:
  DocChildNode() = default;

 public:
  explicit DocChildNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(tag))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, tag);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, tag);
  }

  std::string tag;
};

class DocNode : public NodeFor<DocNode> {
  APPTRAVERSE_OBJECT(DocNode, Node, 0)

 protected:
  DocNode() = default;

 public:
  explicit DocNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label), AE_MMBR(child))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, label, child);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, label, child);
  }

  std::string label;
  ae::ObjPtr<DocChildNode> child;

  void Apply(DocChangedEvent const& event);
};

class DocChangedEvent : public EventFor<DocNode, DocChangedEvent> {
  APPTRAVERSE_OBJECT(DocChangedEvent, Event, 0)

 protected:
  DocChangedEvent() = default;

 public:
  explicit DocChangedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label))

  std::string label;
};

APPTRAVERSE_REGISTER(DocNode);
APPTRAVERSE_REGISTER(DocChildNode);
APPTRAVERSE_REGISTER(DocChangedEvent);

void DocNode::Apply(DocChangedEvent const& event) {
  label = event.label;
}

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (false)

std::size_t CountPersistedLayerFiles(std::filesystem::path const& root) {
  if (!std::filesystem::exists(root)) {
    return 0;
  }
  std::size_t count = 0;
  for (auto const& obj_entry : std::filesystem::directory_iterator(root)) {
    if (!obj_entry.is_directory()) {
      continue;
    }
    for (auto const& class_entry :
         std::filesystem::directory_iterator(obj_entry.path())) {
      if (!class_entry.is_directory()) {
        continue;
      }
      for (auto const& version_entry :
           std::filesystem::directory_iterator(class_entry.path())) {
        if (version_entry.is_regular_file()) {
          ++count;
        }
      }
    }
  }
  return count;
}

std::size_t CountClassLayersForObject(std::filesystem::path const& root,
                                      std::uint32_t obj_id) {
  auto const obj_dir = root / std::to_string(obj_id);
  if (!std::filesystem::exists(obj_dir)) {
    return 0;
  }
  std::size_t count = 0;
  for (auto const& entry : std::filesystem::directory_iterator(obj_dir)) {
    if (entry.is_directory()) {
      ++count;
    }
  }
  return count;
}

bool StorageContainsClass(ae::RamDomainStorage const& storage,
                          std::uint32_t class_id) {
  for (auto const& [obj_id, class_map_opt] : storage.state) {
    if (!class_map_opt) {
      continue;
    }
    if (class_map_opt->count(class_id) != 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

void RunModelPersistenceTest() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto doc = DocNode::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*doc);
  auto runtime = NetworkState::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*runtime, *doc);
  doc->label = "before";

  static_cast<void>(CommitNetworkAvailable(*runtime, 1));

  std::uint64_t const gen_before = doc->Generation();
  std::size_t const journal_before = doc->journal.size();

  ByteSink snapshot;
  SerializePersistentModelSnapshot(*doc, snapshot);
  CHECK(doc->Generation() == gen_before);
  CHECK(doc->journal.size() == journal_before);
  CHECK(doc->label == "before");

  ae::RamDomainStorage scratch;
  ae::RamDomainStorage load_storage;
  ae::Domain load_domain{load_storage};
  auto load_doc =
      DocNode::ptr::Create(ae::CreateWith{load_domain}.with_id(doc->obj_id));
  InitializeRuntimeNode(*load_doc);
  ByteSource in;
  in.data = snapshot.bytes.data();
  in.size = snapshot.bytes.size();
  in.pos = 0;
  LoadPersistentModelSnapshot(in, load_domain, load_storage, *load_doc);
  CHECK(load_doc->label == "before");
  CHECK(!StorageContainsClass(load_storage, NetworkState::kClassId));

  auto fresh_runtime = NetworkState::ptr::Create(ae::CreateWith{load_domain});
  InitializeRuntimeNode(*fresh_runtime, *load_doc);
  static_cast<void>(CommitNetworkInitializing(*fresh_runtime, 2));
  CHECK(fresh_runtime->GetAvailability() == NetworkAvailability::kInitializing);

  auto event = DocChangedEvent::ptr::Create(ae::CreateWith{load_domain});
  event->label = "after";
  load_doc->Commit(event);
  CHECK(load_doc->label == "after");
}

void RunSaveToStorageDoesNotMutateLiveGraphTest() {
  ae::RamDomainStorage ram;
  ae::Domain domain{ram};
  auto doc = DocNode::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*doc);
  doc->label = "checkpoint";

  std::uint64_t const gen_before = doc->Generation();
  std::size_t const journal_before = doc->journal.size();

  auto const temp =
      std::filesystem::temp_directory_path() /
      ("apptraverse_save_to_storage_" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code ec;
  std::filesystem::remove_all(temp, ec);

  apptraverse::DirectoryDomainStorage durable{temp};
  CHECK(apptraverse::SavePersistentModelSnapshotToStorage(*doc, durable));

  CHECK(doc->Generation() == gen_before);
  CHECK(doc->journal.size() == journal_before);
  CHECK(doc->label == "checkpoint");

  apptraverse::PartitionedModelDomainStorage load_model{durable};
  ae::Domain load_domain{load_model};
  auto load_doc =
      DocNode::ptr::Declare(ae::CreateWith{load_domain}.with_id(doc->obj_id));
  load_doc.Load();
  CHECK(load_doc);
  CHECK(load_doc->label == "checkpoint");
}

void RunSaveToStorageColdDirectoryRoundTripTest() {
  auto const temp =
      std::filesystem::temp_directory_path() /
      ("apptraverse_save_cold_dir_" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code ec;
  std::filesystem::remove_all(temp, ec);

  std::uint32_t root_id = 0;
  std::uint32_t child_id = 0;

  {
    ae::RamDomainStorage ram;
    ae::Domain domain{ram};
    auto doc = DocNode::ptr::Create(ae::CreateWith{domain});
    InitializeRuntimeNode(*doc);
    auto child = DocChildNode::ptr::Create(ae::CreateWith{domain});
    InitializeRuntimeNode(*child);
    doc->label = "persisted-root";
    child->tag = "persisted-child";
    doc->child = child;

    apptraverse::DirectoryDomainStorage durable{temp};
    CHECK(apptraverse::SavePersistentModelSnapshotToStorage(*doc, durable));

    root_id = doc->obj_id.id();
    child_id = child->obj_id.id();
    CHECK(root_id != 0);
    CHECK(child_id != 0);

    CHECK(CountClassLayersForObject(temp, root_id) >= 1);
    CHECK(CountPersistedLayerFiles(temp) >= 2);
  }

  {
    apptraverse::DirectoryDomainStorage durable{temp};
    ae::Domain domain{durable};
    auto load_doc =
        DocNode::ptr::Declare(ae::CreateWith{domain}.with_id(ae::ObjId{root_id}));
    load_doc.Load();
    CHECK(load_doc);
    CHECK(load_doc->label == "persisted-root");
    CHECK(load_doc->child.is_valid());
    CHECK(load_doc->child->obj_id.id() == child_id);
    CHECK(load_doc->child->tag == "persisted-child");
  }

  std::filesystem::remove_all(temp, ec);
}

void RunPersistentJournalCheckpointRoundTripTest() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto doc = DocNode::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*doc);
  CHECK(doc->base.is_valid());
  doc->SetJournalRetentionPolicy(JournalRetentionPolicy{.max_events = 10});
  doc->label = "v0";

  auto ev1 = DocChangedEvent::ptr::Create(ae::CreateWith{domain});
  ev1->label = "v1";
  doc->Commit(ev1);
  auto ev2 = DocChangedEvent::ptr::Create(ae::CreateWith{domain});
  ev2->label = "v2";
  doc->Commit(ev2);
  CHECK(doc->label == "v2");
  CHECK(doc->journal.size() >= 2);

  std::size_t const journal_before = doc->journal.size();
  std::uint64_t const gen_before = doc->Generation();

  ByteSink snapshot;
  SerializePersistentModelSnapshot(*doc, snapshot);
  CHECK(doc->journal.size() == journal_before);
  CHECK(doc->Generation() == gen_before);

  ae::RamDomainStorage load_storage;
  ae::Domain load_domain{load_storage};
  auto load_doc =
      DocNode::ptr::Create(ae::CreateWith{load_domain}.with_id(doc->obj_id));
  InitializeRuntimeNode(*load_doc);
  ByteSource in;
  in.data = snapshot.bytes.data();
  in.size = snapshot.bytes.size();
  LoadPersistentModelSnapshot(in, load_domain, load_storage, *load_doc);
  CHECK(load_doc->label == "v2");
  CHECK(load_doc->journal.size() == journal_before);
  CHECK(load_doc->base.is_valid());

  auto ev3 = DocChangedEvent::ptr::Create(ae::CreateWith{load_domain});
  ev3->label = "v3";
  load_doc->Commit(ev3);
  CHECK(load_doc->label == "v3");

  ByteSink snapshot2;
  SerializePersistentModelSnapshot(*load_doc, snapshot2);
  ae::RamDomainStorage load_storage2;
  ae::Domain load_domain2{load_storage2};
  auto load_doc2 =
      DocNode::ptr::Create(ae::CreateWith{load_domain2}.with_id(doc->obj_id));
  InitializeRuntimeNode(*load_doc2);
  ByteSource in2;
  in2.data = snapshot2.bytes.data();
  in2.size = snapshot2.bytes.size();
  LoadPersistentModelSnapshot(in2, load_domain2, load_storage2, *load_doc2);
  CHECK(load_doc2->label == "v3");
  CHECK(load_doc2->journal.size() >= journal_before);
}

void RunCheckpointSidecarStorageEquivalenceTest() {
  auto const temp =
      std::filesystem::temp_directory_path() /
      ("apptraverse_sidecar_equiv_" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code ec;
  std::filesystem::remove_all(temp, ec);

  ae::RamDomainStorage ram;
  ae::Domain domain{ram};
  auto doc = DocNode::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*doc);
  doc->label = "equiv";

  apptraverse::DirectoryDomainStorage durable{temp};
  CHECK(SavePersistentModelSnapshotToStorage(*doc, durable));
  CHECK(std::filesystem::exists(temp / "persistent_model_checkpoint.bin"));

  std::filesystem::remove_all(temp, ec);
  apptraverse::DirectoryDomainStorage durable2{temp};
  ae::IDomainStorage& storage_ref = durable2;
  CHECK(SavePersistentModelSnapshotToStorage(*doc, storage_ref, durable2.root()));
  CHECK(std::filesystem::exists(temp / "persistent_model_checkpoint.bin"));

  apptraverse::PartitionedModelDomainStorage partitioned{durable2};
  ae::Domain load_domain{partitioned};
  auto load_doc =
      DocNode::ptr::Create(ae::CreateWith{load_domain}.with_id(doc->obj_id));
  CHECK(TryLoadPersistedModelFromStorageSidecar(load_domain, partitioned,
                                                *load_doc));
  CHECK(load_doc->label == "equiv");

  ae::RamDomainStorage ram_backing;
  apptraverse::PartitionedModelDomainStorage ram_partition{ram_backing};
  CHECK(SavePersistentModelSnapshotToStorage(*doc, ram_partition));
  CHECK(!ram_partition.checkpoint_sidecar_directory());

  std::filesystem::remove_all(temp, ec);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunModelPersistenceTest();
  apptraverse::test::RunSaveToStorageDoesNotMutateLiveGraphTest();
  apptraverse::test::RunSaveToStorageColdDirectoryRoundTripTest();
  apptraverse::test::RunPersistentJournalCheckpointRoundTripTest();
  apptraverse::test::RunCheckpointSidecarStorageEquivalenceTest();
  return 0;
}
