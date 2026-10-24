#include "distilled_lazy_graph_bootstrap.h"

#include <cstdlib>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/distill.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/overlay_domain_storage.h"

namespace apptraverse::test {
namespace {

class DocNode;
class LazyDistilledRoot;

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

APPTRAVERSE_REGISTER(DocNode);
APPTRAVERSE_REGISTER(DocChildNode);
APPTRAVERSE_REGISTER(LazyDistilledRoot);

}  // namespace

ae::ObjId DistillLazyDocGraphIntoStorage(ae::RamDomainStorage& storage,
                                         ae::RamDomainStorage* export_storage) {
  ae::ObjId const root_id{43000};
  ae::ObjId const first_id{43001};
  ae::ObjId const second_id{43002};

  ae::Domain domain{storage};
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
  StabilizeDistilledPersistentSnapshot(*root);
  SaveDistilledRoot(*root);
  if (export_storage != nullptr) {
    SavePersistedObjectGraphToRam(*root, *export_storage);
  }
  return root_id;
}

void EnsureLazyDistilledGraphRegistration() {
  (void)&g_apptraverse_registrar_LazyDistilledRoot;
  (void)&g_apptraverse_registrar_DocNode;
  (void)&g_apptraverse_registrar_DocChildNode;
}

void RunEmbeddedLazyDistilledConsumerChecks(
    EmbeddedDistilledGraph const& graph) {
  ae::RamDomainStorage backing;
  InstallEmbeddedDistilledBacking(backing, graph);
  ae::RamDomainStorage::ObjClassData const backing_snapshot = backing.state;

  ae::RamDomainStorage user_storage;
  OverlayDomainStorage overlay{user_storage, backing, {}};
  ae::Domain domain{overlay};

  ae::ObjId const root_id{43000};
  ae::ObjId const first_id{43001};
  ae::ObjId const second_id{43002};

  ae::DomainGraph load_graph{&domain};
  ae::Ptr<ae::Obj> loaded;
  if (!load_graph.LoadRoot(root_id, loaded).IsOk()) {
    std::abort();
  }
  ae::Ptr<LazyDistilledRoot> root{loaded};
  if (root->first.id() != first_id || root->second.id() != second_id ||
      root->first.is_loaded() || root->second.is_loaded()) {
    std::abort();
  }

  ae::DomainQuery const second_doc_v0{second_id, DocNode::kClassId, 0};
  if (overlay.Load(second_doc_v0).result != ae::DomainLoadResult::kLoaded) {
    std::abort();
  }

  ae::Ptr<DocNode> second = root->second.Load();
  if (second->label != "second-initial" || !second->base.is_valid() ||
      backing.state != backing_snapshot || root->first.is_loaded()) {
    std::abort();
  }
}

}  // namespace apptraverse::test
