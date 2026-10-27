#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj.h"

#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/event_for.h"
#include "apptraverse/journal_retention_policy.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse::test {
namespace {

class DocNode;
class DocChildNode;
class DocChangedEvent;
class EphemeralObserverNode;

class EphemeralObserverNode : public NodeFor<EphemeralObserverNode> {
  APPTRAVERSE_OBJECT(EphemeralObserverNode, Node, 0)

 protected:
  EphemeralObserverNode() = default;

 public:
  explicit EphemeralObserverNode(ae::ObjProp prop) : NodeFor{prop}, run_id{0} {}

  AE_OBJECT_REFLECT(AE_MMBR(run_id))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, run_id);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, run_id);
  }

  std::uint64_t run_id;
};

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

class LazyDistilledRoot : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::LazyDistilledRoot",
                           LazyDistilledRoot, ae::Obj, 0)

 protected:
  LazyDistilledRoot() = default;

 public:
  explicit LazyDistilledRoot(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(first), AE_MMBR(second))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, first, second);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, first, second);
  }

  DocNode::ptr first;
  DocNode::ptr second;
};

APPTRAVERSE_REGISTER(EphemeralObserverNode);
APPTRAVERSE_REGISTER(DocNode);
APPTRAVERSE_REGISTER(DocChildNode);
APPTRAVERSE_REGISTER(DocChangedEvent);
APPTRAVERSE_REGISTER(LazyDistilledRoot);

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

}  // namespace

void RunModelPersistenceTest() {
  auto const temp =
      std::filesystem::temp_directory_path() /
      ("apptraverse_model_persist_" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code ec;
  std::filesystem::remove_all(temp, ec);

  ae::ObjId doc_id;
  ae::ObjId runtime_id;
  std::uint64_t gen_before = 0;
  std::size_t journal_before = 0;

  {
    apptraverse::DirectoryDomainStorage durable{temp};
    ae::Domain domain{durable};
    auto doc = DocNode::ptr::Create(ae::CreateWith{domain});
    InitializeRuntimeNode(*doc);
    auto runtime = EphemeralObserverNode::ptr::Create(ae::CreateWith{domain});
    InitializeRuntimeNode(*runtime, *doc);
    runtime->run_id = 1;
    doc->label = "before";
    doc_id = doc->obj_id;
    runtime_id = runtime->obj_id;
    gen_before = doc->Generation();
    journal_before = doc->journal.size();

    durable.BeginSaveSession();
    ae::DomainGraph save_graph{&domain};
    ae::Ptr<ae::Obj> save_root = domain.Find(doc_id);
    CHECK(save_root);
    CHECK(save_graph.SaveRoot(save_root, doc_id).IsOk());
    CHECK(!durable.SaveSessionHadFailure());

    CHECK(doc->Generation() == gen_before);
    CHECK(doc->journal.size() == journal_before);
    CHECK(doc->label == "before");
  }

  {
    apptraverse::DirectoryDomainStorage durable{temp};
    ae::Domain load_domain{durable};
    ae::DomainGraph load_graph{&load_domain};
    ae::Ptr<ae::Obj> loaded_obj;
    CHECK(load_graph.LoadRoot(doc_id, loaded_obj).IsOk());
    ae::Ptr<DocNode> load_doc{loaded_obj};
    CHECK(load_doc->label == "before");
    CHECK(!load_domain.Find(runtime_id));

    auto fresh_runtime =
        EphemeralObserverNode::ptr::Create(ae::CreateWith{load_domain});
    InitializeRuntimeNode(*fresh_runtime, *load_doc);
    fresh_runtime->run_id = 2;
    CHECK(fresh_runtime->run_id == 2);

    auto event = DocChangedEvent::ptr::Create(ae::CreateWith{load_domain});
    event->label = "after";
    load_doc->Commit(event);
    CHECK(load_doc->label == "after");
  }

  std::filesystem::remove_all(temp, ec);
}

void RunSaveToStorageDoesNotMutateLiveGraphTest() {
  auto const temp =
      std::filesystem::temp_directory_path() /
      ("apptraverse_save_to_storage_" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code ec;
  std::filesystem::remove_all(temp, ec);

  apptraverse::DirectoryDomainStorage durable{temp};
  ae::Domain domain{durable};
  auto doc = DocNode::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*doc);
  doc->label = "checkpoint";
  ae::ObjId const doc_id = doc->obj_id;

  std::uint64_t const gen_before = doc->Generation();
  std::size_t const journal_before = doc->journal.size();

  durable.BeginSaveSession();
  ae::DomainGraph save_graph{&domain};
  ae::Ptr<ae::Obj> save_root = domain.Find(doc_id);
  CHECK(save_root);
  CHECK(save_graph.SaveRoot(save_root, doc_id).IsOk());
  CHECK(!durable.SaveSessionHadFailure());

  CHECK(doc->Generation() == gen_before);
  CHECK(doc->journal.size() == journal_before);
  CHECK(doc->label == "checkpoint");

  apptraverse::DirectoryDomainStorage load_durable{temp};
  ae::Domain load_domain{load_durable};
  ae::DomainGraph load_graph{&load_domain};
  ae::Ptr<ae::Obj> loaded_obj;
  CHECK(load_graph.LoadRoot(doc_id, loaded_obj).IsOk());
  ae::Ptr<DocNode> load_doc{loaded_obj};
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
    apptraverse::DirectoryDomainStorage durable{temp};
    ae::Domain domain{durable};
    auto doc = DocNode::ptr::Create(ae::CreateWith{domain});
    InitializeRuntimeNode(*doc);
    auto child = DocChildNode::ptr::Create(ae::CreateWith{domain});
    InitializeRuntimeNode(*child);
    doc->label = "persisted-root";
    child->tag = "persisted-child";
    doc->child = child;

    ae::ObjId const doc_id = doc->obj_id;
    durable.BeginSaveSession();
    ae::DomainGraph save_graph{&domain};
    ae::Ptr<ae::Obj> save_root = domain.Find(doc_id);
    CHECK(save_root);
    CHECK(save_graph.SaveRoot(save_root, doc_id).IsOk());
    CHECK(!durable.SaveSessionHadFailure());

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
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded_obj;
    CHECK(load_graph.LoadRoot(ae::ObjId{root_id}, loaded_obj).IsOk());
    ae::Ptr<DocNode> load_doc{loaded_obj};
    CHECK(load_doc->label == "persisted-root");
    CHECK(load_doc->child.is_valid());
    CHECK(load_doc->child->obj_id.id() == child_id);
    CHECK(load_doc->child->tag == "persisted-child");
  }

  std::filesystem::remove_all(temp, ec);
}

void RunStandardDirectoryStorageNodeJournalRoundTripTest() {
  auto const temp =
      std::filesystem::temp_directory_path() /
      ("apptraverse_std_storage_journal_" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code ec;
  std::filesystem::remove_all(temp, ec);

  ae::ObjId doc_id;
  std::size_t journal_after_two_commits = 0;

  {
    apptraverse::DirectoryDomainStorage durable{temp};
    ae::Domain domain{durable};
    auto doc = DocNode::ptr::Create(ae::CreateWith{domain});
    InitializeRuntimeNode(*doc);
    CHECK(doc->base.is_valid());
    doc->label = "v0";

    auto ev1 = DocChangedEvent::ptr::Create(ae::CreateWith{domain});
    ev1->label = "v1";
    doc->Commit(ev1);
    auto ev2 = DocChangedEvent::ptr::Create(ae::CreateWith{domain});
    ev2->label = "v2";
    doc->Commit(ev2);
    CHECK(doc->label == "v2");
    journal_after_two_commits = doc->journal.size();
    CHECK(journal_after_two_commits >= 2);
    doc_id = doc->obj_id;

    durable.BeginSaveSession();
    ae::DomainGraph save_graph{&domain};
    ae::Ptr<ae::Obj> save_root = domain.Find(doc_id);
    CHECK(save_root);
    CHECK(save_graph.SaveRoot(save_root, doc_id).IsOk());
    CHECK(!durable.SaveSessionHadFailure());
  }

  {
    apptraverse::DirectoryDomainStorage durable{temp};
    ae::Domain domain{durable};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded_obj;
    CHECK(load_graph.LoadRoot(doc_id, loaded_obj).IsOk());
    ae::Ptr<DocNode> doc{loaded_obj};
    CHECK(doc->label == "v2");
    CHECK(doc->base.is_valid());
    CHECK(doc->journal.size() == journal_after_two_commits);

    auto ev3 = DocChangedEvent::ptr::Create(ae::CreateWith{domain});
    ev3->label = "v3";
    doc->Commit(ev3);
    CHECK(doc->label == "v3");

    durable.BeginSaveSession();
    ae::DomainGraph save_graph{&domain};
    ae::Ptr<ae::Obj> save_root = domain.Find(doc_id);
    CHECK(save_root);
    CHECK(save_graph.SaveRoot(save_root, doc_id).IsOk());
    CHECK(!durable.SaveSessionHadFailure());
  }

  {
    apptraverse::DirectoryDomainStorage durable{temp};
    ae::Domain domain{durable};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded_obj;
    CHECK(load_graph.LoadRoot(doc_id, loaded_obj).IsOk());
    ae::Ptr<DocNode> doc{loaded_obj};
    CHECK(doc->label == "v3");
    CHECK(doc->base.is_valid());
    CHECK(doc->journal.size() >= journal_after_two_commits + 1);

    auto ev4 = DocChangedEvent::ptr::Create(ae::CreateWith{domain});
    ev4->label = "v4";
    doc->Commit(ev4);
    CHECK(doc->label == "v4");
  }

  std::filesystem::remove_all(temp, ec);
}


void RunDistilledLazyOverlayTest() {
  std::filesystem::path const user_root =
      std::filesystem::temp_directory_path() /
      "apptraverse_distilled_lazy_overlay_user_state";
  std::filesystem::remove_all(user_root);

  ae::RamDomainStorage backing;
  ae::ObjId const root_id{43000};
  ae::ObjId const first_id{43001};
  ae::ObjId const second_id{43002};
  ae::DomainQuery const first_doc_v0{first_id, DocNode::kClassId, 0};
  ae::DomainQuery const second_doc_v0{second_id, DocNode::kClassId, 0};

  ae::ObjId first_base_id{};
  ae::ObjId second_base_id{};

  {
    ae::Domain domain{backing};
    LazyDistilledRoot::ptr root_ref =
        LazyDistilledRoot::ptr::Create(ae::CreateWith{domain}.with_id(root_id));
    ae::Ptr<LazyDistilledRoot> root = root_ref.Load();
    DocNode::ptr first_ref = DocNode::ptr::Create(
        ae::CreateWith{domain}
            .with_id(first_id)
            .with_flags(ae::ObjFlags::kUnloadedByDefault));
    ae::Ptr<DocNode> first = first_ref.Load();
    first->label = "first-initial";
    DocNode::ptr second_ref = DocNode::ptr::Create(
        ae::CreateWith{domain}
            .with_id(second_id)
            .with_flags(ae::ObjFlags::kUnloadedByDefault));
    ae::Ptr<DocNode> second = second_ref.Load();
    second->label = "second-initial";
    root->first = first_ref;
    root->second = second_ref;

    FinalizeDistilledGraph(*root);
    CHECK(first->base.is_valid());
    CHECK(first->base.is_loaded());
    CHECK(first->journal.empty());
    CHECK(second->base.is_valid());
    CHECK(second->base.is_loaded());
    CHECK(second->journal.empty());
    first_base_id = first->base->obj_id;
    second_base_id = second->base->obj_id;

    SaveDistilledRoot(*root);

    CHECK(backing.state.count(root_id) == 1);
    CHECK(backing.state.count(first_id) == 1);
    CHECK(backing.state.count(second_id) == 1);
    CHECK(backing.state.count(first_base_id) == 1);
    CHECK(backing.state.count(second_base_id) == 1);
    CHECK(backing.state.at(root_id)->count(LazyDistilledRoot::kClassId) == 1);
    CHECK(backing.state.at(first_id)->count(DocNode::kClassId) == 1);
    CHECK(backing.state.at(second_id)->count(DocNode::kClassId) == 1);
    CHECK(backing.state.at(first_base_id)->count(Node::kClassId) == 1);
    CHECK(backing.state.at(second_base_id)->count(Node::kClassId) == 1);
  }

  ae::RamDomainStorage::ObjClassData const backing_snapshot = backing.state;

  {
    DirectoryDomainStorage user_storage{user_root};
    OverlayDomainStorage overlay{user_storage, backing};
    ae::Domain domain{overlay};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(root_id, loaded).IsOk());
    ae::Ptr<LazyDistilledRoot> root{loaded};
    CHECK(root->first.id() == first_id);
    CHECK(root->second.id() == second_id);
    CHECK(!root->first.is_loaded());
    CHECK(!root->second.is_loaded());

    ae::Ptr<DocNode> first = root->first.Load();
    CHECK(first->label == "first-initial");
    CHECK(first->base.is_valid());
    CHECK(first->journal.empty());

    auto event = DocChangedEvent::ptr::Create(ae::CreateWith{domain});
    event->label = "first-updated";
    first->Commit(event);
    CHECK(first->label == "first-updated");
    CHECK(first->journal.size() == 1);
    CHECK(!root->second.is_loaded());

    user_storage.BeginSaveSession();
    ae::DomainGraph save_graph{&domain};
    CHECK(save_graph.SaveRoot(root, root_id).IsOk());
    CHECK(!user_storage.SaveSessionHadFailure());
    CHECK(first->base->obj_id == first_base_id);
    CHECK(DirectoryDomainStorageLayerFileExists(user_root, first_doc_v0));
    CHECK(DirectoryDomainStorageLayerFileExists(
        user_root, ae::DomainQuery{first_base_id, ae::Obj::kClassId, 0}));
    CHECK(DirectoryDomainStorageLayerFileExists(
        user_root, ae::DomainQuery{first_base_id, Node::kClassId, 0}));
    CHECK(DirectoryDomainStorageLayerFileExists(
        user_root, ae::DomainQuery{first_base_id, DocNode::kClassId, 0}));
    CHECK(!DirectoryDomainStorageLayerFileExists(
        user_root, ae::DomainQuery{second_base_id, ae::Obj::kClassId, 0}));
    CHECK(!DirectoryDomainStorageLayerFileExists(
        user_root, ae::DomainQuery{second_base_id, Node::kClassId, 0}));
    CHECK(!DirectoryDomainStorageLayerFileExists(
        user_root, ae::DomainQuery{second_base_id, DocNode::kClassId, 0}));
    CHECK(CountClassLayersForObject(user_root, second_id.id()) == 0);
    CHECK(root->second.id() == second_id);
    CHECK(backing.state == backing_snapshot);
  }

  {
    DirectoryDomainStorage user_storage{user_root};
    OverlayDomainStorage overlay{user_storage, backing};
    ae::Domain domain{overlay};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(root_id, loaded).IsOk());
    ae::Ptr<LazyDistilledRoot> root{loaded};
    CHECK(!root->first.is_loaded());
    CHECK(!root->second.is_loaded());

    ae::Ptr<DocNode> second = root->second.Load();
    CHECK(second->label == "second-initial");
    CHECK(second->base.is_valid());
    CHECK(second->journal.empty());

    auto event = DocChangedEvent::ptr::Create(ae::CreateWith{domain});
    event->label = "second-updated";
    second->Commit(event);
    CHECK(second->journal.size() == 1);

    user_storage.BeginSaveSession();
    ae::DomainGraph save_graph{&domain};
    CHECK(save_graph.SaveRoot(root, root_id).IsOk());
    CHECK(!user_storage.SaveSessionHadFailure());
    CHECK(!root->first.is_loaded());
    CHECK(DirectoryDomainStorageLayerFileExists(user_root, first_doc_v0));
    CHECK(DirectoryDomainStorageLayerFileExists(user_root, second_doc_v0));
    CHECK(second->base->obj_id == second_base_id);
    CHECK(DirectoryDomainStorageLayerFileExists(
        user_root, ae::DomainQuery{second_base_id, ae::Obj::kClassId, 0}));
    CHECK(DirectoryDomainStorageLayerFileExists(
        user_root, ae::DomainQuery{second_base_id, Node::kClassId, 0}));
    CHECK(DirectoryDomainStorageLayerFileExists(
        user_root, ae::DomainQuery{second_base_id, DocNode::kClassId, 0}));
    CHECK(DirectoryDomainStorageLayerFileExists(
        user_root, ae::DomainQuery{first_base_id, ae::Obj::kClassId, 0}));
    CHECK(DirectoryDomainStorageLayerFileExists(
        user_root, ae::DomainQuery{first_base_id, Node::kClassId, 0}));
    CHECK(DirectoryDomainStorageLayerFileExists(
        user_root, ae::DomainQuery{first_base_id, DocNode::kClassId, 0}));
    CHECK(backing.state == backing_snapshot);
  }

  {
    DirectoryDomainStorage user_storage{user_root};
    OverlayDomainStorage overlay{user_storage, backing};
    ae::Domain domain{overlay};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(root_id, loaded).IsOk());
    ae::Ptr<LazyDistilledRoot> root{loaded};
    CHECK(root->first.id() == first_id);
    CHECK(root->second.id() == second_id);
    CHECK(!root->first.is_loaded());
    CHECK(!root->second.is_loaded());

    ae::Ptr<DocNode> first = root->first.Load();
    ae::Ptr<DocNode> second = root->second.Load();
    CHECK(first->label == "first-updated");
    CHECK(second->label == "second-updated");
    CHECK(first->journal.size() == 1);
    CHECK(second->journal.size() == 1);
    CHECK(first->base.is_valid());
    CHECK(first->base.is_loaded());
    CHECK(second->base.is_valid());
    CHECK(second->base.is_loaded());
    CHECK(first->obj_id == first_id);
    CHECK(second->obj_id == second_id);

    first->label = "stale-first";
    CHECK(first->TryReplayFromBase());
    CHECK(first->label == "first-updated");
    second->label = "stale-second";
    CHECK(second->TryReplayFromBase());
    CHECK(second->label == "second-updated");
    CHECK(backing.state == backing_snapshot);
  }

  ae::RamDomainStorage replacement_backing;
  ae::ObjId replacement_first_base_id{};
  ae::ObjId replacement_second_base_id{};
  {
    ae::Domain domain{replacement_backing};
    LazyDistilledRoot::ptr root_ref =
        LazyDistilledRoot::ptr::Create(ae::CreateWith{domain}.with_id(root_id));
    ae::Ptr<LazyDistilledRoot> root = root_ref.Load();
    DocNode::ptr first_ref = DocNode::ptr::Create(
        ae::CreateWith{domain}
            .with_id(first_id)
            .with_flags(ae::ObjFlags::kUnloadedByDefault));
    ae::Ptr<DocNode> first = first_ref.Load();
    first->label = "first-initial";
    DocNode::ptr second_ref = DocNode::ptr::Create(
        ae::CreateWith{domain}
            .with_id(second_id)
            .with_flags(ae::ObjFlags::kUnloadedByDefault));
    ae::Ptr<DocNode> second = second_ref.Load();
    second->label = "second-initial";
    root->first = first_ref;
    root->second = second_ref;

    FinalizeDistilledGraph(*root);
    SaveDistilledRoot(*root);

    replacement_first_base_id = first->base->obj_id;
    replacement_second_base_id = second->base->obj_id;

    CHECK(replacement_backing.state.count(root_id) == 1);
    CHECK(replacement_backing.state.count(first_id) == 1);
    CHECK(replacement_backing.state.count(second_id) == 1);
    CHECK(replacement_backing.state.count(replacement_first_base_id) == 1);
    CHECK(replacement_backing.state.count(replacement_second_base_id) == 1);
    CHECK(replacement_backing.state.at(first_id)->count(DocNode::kClassId) == 1);
    CHECK(replacement_backing.state.at(second_id)->count(DocNode::kClassId) == 1);
    CHECK(replacement_backing.state.at(replacement_first_base_id)
              ->count(Node::kClassId) == 1);
    CHECK(replacement_backing.state.at(replacement_second_base_id)
              ->count(Node::kClassId) == 1);
  }

  ae::RamDomainStorage::ObjClassData const replacement_backing_snapshot =
      replacement_backing.state;

  {
    DirectoryDomainStorage user_storage{user_root};
    user_storage.BeginSaveSession();
    OverlayDomainStorage overlay{user_storage, replacement_backing};
    ae::Domain domain{overlay};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(root_id, loaded).IsOk());
    ae::Ptr<LazyDistilledRoot> root{loaded};
    CHECK(root->obj_id == root_id);
    CHECK(root->first.id() == first_id);
    CHECK(root->second.id() == second_id);
    CHECK(!root->first.is_loaded());
    CHECK(!root->second.is_loaded());

    ae::Ptr<DocNode> first = root->first.Load();
    if (!first) {
      std::cerr << "stage E2: First failed to load; old base="
                << first_base_id.id() << " new base="
                << replacement_first_base_id.id() << " old base in USER="
                << DirectoryDomainStorageLayerFileExists(
                       user_root,
                       ae::DomainQuery{first_base_id, Node::kClassId, 0})
                << " old base in replacement="
                << (replacement_backing.state.count(first_base_id) != 0)
                << '\n';
    }
    CHECK(first);
    ae::Ptr<DocNode> second = root->second.Load();
    if (!second) {
      std::cerr << "stage E2: Second failed to load; old base="
                << second_base_id.id() << " new base="
                << replacement_second_base_id.id() << " old base in USER="
                << DirectoryDomainStorageLayerFileExists(
                       user_root,
                       ae::DomainQuery{second_base_id, Node::kClassId, 0})
                << " old base in replacement="
                << (replacement_backing.state.count(second_base_id) != 0)
                << '\n';
    }
    CHECK(second);

    CHECK(first->label == "first-updated");
    CHECK(second->label == "second-updated");
    CHECK(first->journal.size() == 1);
    CHECK(second->journal.size() == 1);
    CHECK(first->base.is_valid());
    CHECK(first->base.is_loaded());
    CHECK(second->base.is_valid());
    CHECK(second->base.is_loaded());
    CHECK(first->obj_id == first_id);
    CHECK(second->obj_id == second_id);
    CHECK(first->base.id() == first_base_id);
    CHECK(second->base.id() == second_base_id);
    CHECK(first->base.id() != replacement_first_base_id);
    CHECK(second->base.id() != replacement_second_base_id);
    CHECK(static_cast<DocNode&>(*first->base).label == "first-initial");
    CHECK(static_cast<DocNode&>(*second->base).label == "second-initial");

    first->label = "stale-first";
    CHECK(first->TryReplayFromBase());
    CHECK(first->label == "first-updated");
    second->label = "stale-second";
    CHECK(second->TryReplayFromBase());
    CHECK(second->label == "second-updated");

    DirectoryDomainStorageStoreStats const stats =
        user_storage.SnapshotSaveSessionStats();
    CHECK(stats.layer_store_calls == 0);
    CHECK(replacement_backing.state == replacement_backing_snapshot);
    CHECK(backing.state == backing_snapshot);
  }

  std::filesystem::path const compact_user_root =
      std::filesystem::temp_directory_path() /
      "apptraverse_distilled_lazy_compact_user_state";
  std::filesystem::remove_all(compact_user_root);

  {
    DirectoryDomainStorage user_storage{compact_user_root};
    OverlayDomainStorage overlay{user_storage, backing};
    ae::Domain domain{overlay};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(root_id, loaded).IsOk());
    ae::Ptr<LazyDistilledRoot> root{loaded};
    CHECK(root->first.id() == first_id);
    CHECK(root->second.id() == second_id);
    CHECK(!root->first.is_loaded());
    CHECK(!root->second.is_loaded());

    ae::Ptr<DocNode> first = root->first.Load();
    CHECK(first);
    CHECK(first->label == "first-initial");
    CHECK(first->journal.empty());
    CHECK(first->base.is_valid());
    CHECK(first->base.is_loaded());
    CHECK(first->base.id() == first_base_id);
    CHECK(!root->second.is_loaded());

    auto event = DocChangedEvent::ptr::Create(ae::CreateWith{domain});
    event->label = "first-compacted";
    first->Commit(event);
    CHECK(first->label == "first-compacted");
    CHECK(first->journal.size() == 1);

    first->SetJournalRetentionPolicy(JournalRetentionPolicy{.max_events = 0});
    first->CompactJournal(SystemUtcMicros());
    CHECK(first->label == "first-compacted");
    CHECK(first->journal.empty());
    CHECK(first->base.id() == first_base_id);
    CHECK(static_cast<DocNode&>(*first->base).label == "first-compacted");
    CHECK(!root->second.is_loaded());
    CHECK(DirectoryDomainStorageLayerFileExists(
        compact_user_root, ae::DomainQuery{first_id, Node::kClassId, 0}));
    CHECK(DirectoryDomainStorageLayerFileExists(
        compact_user_root, ae::DomainQuery{first_base_id, ae::Obj::kClassId, 0}));
    CHECK(DirectoryDomainStorageLayerFileExists(
        compact_user_root, ae::DomainQuery{first_base_id, Node::kClassId, 0}));
    CHECK(DirectoryDomainStorageLayerFileExists(
        compact_user_root, ae::DomainQuery{first_base_id, DocNode::kClassId, 0}));
    CHECK(CountClassLayersForObject(compact_user_root, second_id.id()) == 0);
    CHECK(!DirectoryDomainStorageLayerFileExists(
        compact_user_root, ae::DomainQuery{second_base_id, ae::Obj::kClassId, 0}));
    CHECK(backing.state == backing_snapshot);

    user_storage.BeginSaveSession();
    ae::DomainGraph save_graph{&domain};
    CHECK(save_graph.SaveRoot(root, root_id).IsOk());
    CHECK(!user_storage.SaveSessionHadFailure());
    CHECK(DirectoryDomainStorageLayerFileExists(compact_user_root, first_doc_v0));
    CHECK(CountClassLayersForObject(compact_user_root, second_id.id()) == 0);
    CHECK(!DirectoryDomainStorageLayerFileExists(
        compact_user_root, ae::DomainQuery{second_base_id, ae::Obj::kClassId, 0}));
    CHECK(backing.state == backing_snapshot);
  }

  {
    DirectoryDomainStorage user_storage{compact_user_root};
    user_storage.BeginSaveSession();
    OverlayDomainStorage overlay{user_storage, replacement_backing};
    ae::Domain domain{overlay};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(root_id, loaded).IsOk());
    ae::Ptr<LazyDistilledRoot> root{loaded};
    CHECK(root->obj_id == root_id);
    CHECK(root->first.id() == first_id);
    CHECK(root->second.id() == second_id);
    CHECK(!root->first.is_loaded());
    CHECK(!root->second.is_loaded());

    ae::Ptr<DocNode> first = root->first.Load();
    if (!first) {
      std::cerr << "stage F2: First failed to load; expected base="
                << first_base_id.id() << " old base Obj in USER="
                << DirectoryDomainStorageLayerFileExists(
                       compact_user_root,
                       ae::DomainQuery{first_base_id, ae::Obj::kClassId, 0})
                << " Node="
                << DirectoryDomainStorageLayerFileExists(
                       compact_user_root,
                       ae::DomainQuery{first_base_id, Node::kClassId, 0})
                << " DocNode="
                << DirectoryDomainStorageLayerFileExists(
                       compact_user_root,
                       ae::DomainQuery{first_base_id, DocNode::kClassId, 0})
                << '\n';
    }
    CHECK(first);
    if (first->base.id() != first_base_id) {
      std::cerr << "stage F2: base ObjId expected=" << first_base_id.id()
                << " actual=" << first->base.id().id()
                << " replacement_first_base=" << replacement_first_base_id.id()
                << " old base Obj in USER="
                << DirectoryDomainStorageLayerFileExists(
                       compact_user_root,
                       ae::DomainQuery{first_base_id, ae::Obj::kClassId, 0})
                << " Node="
                << DirectoryDomainStorageLayerFileExists(
                       compact_user_root,
                       ae::DomainQuery{first_base_id, Node::kClassId, 0})
                << " DocNode="
                << DirectoryDomainStorageLayerFileExists(
                       compact_user_root,
                       ae::DomainQuery{first_base_id, DocNode::kClassId, 0})
                << " label=" << first->label << " base_label="
                << static_cast<DocNode&>(*first->base).label << '\n';
    }
    CHECK(first->label == "first-compacted");
    CHECK(first->journal.empty());
    CHECK(first->base.is_valid());
    CHECK(first->base.is_loaded());
    CHECK(first->base.id() == first_base_id);
    CHECK(static_cast<DocNode&>(*first->base).label == "first-compacted");
    CHECK(!root->second.is_loaded());

    first->label = "stale-compacted";
    bool const replay_ok = first->TryReplayFromBase();
    if (!replay_ok || first->label != "first-compacted") {
      std::cerr << "stage F2: replay failed ok=" << replay_ok << " label="
                << first->label << " base_id=" << first->base.id().id()
                << " base_label="
                << static_cast<DocNode&>(*first->base).label << '\n';
    }
    CHECK(replay_ok);
    CHECK(first->label == "first-compacted");

    DirectoryDomainStorageStoreStats const stats =
        user_storage.SnapshotSaveSessionStats();
    CHECK(stats.layer_store_calls == 0);
    CHECK(backing.state == backing_snapshot);
    CHECK(replacement_backing.state == replacement_backing_snapshot);
    CHECK(!root->second.is_loaded());
  }

  std::filesystem::remove_all(user_root);
  std::filesystem::remove_all(compact_user_root);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::RunModelPersistenceTest();
  apptraverse::test::RunSaveToStorageDoesNotMutateLiveGraphTest();
  apptraverse::test::RunSaveToStorageColdDirectoryRoundTripTest();
  apptraverse::test::RunStandardDirectoryStorageNodeJournalRoundTripTest();
  apptraverse::test::RunDistilledLazyOverlayTest();
  return 0;
}
