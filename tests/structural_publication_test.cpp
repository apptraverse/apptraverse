#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "aether-objects/domain_storage/ram_domain_storage.h"

#include "apptraverse/node.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"

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

class TestRuntimeNode;
class TestApplication;

class TestRuntimeNode : public NodeFor<TestRuntimeNode> {
  APPTRAVERSE_OBJECT(TestRuntimeNode, Node, 0)

 protected:
  TestRuntimeNode() = default;

 public:
  explicit TestRuntimeNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, value);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, value);
  }

  std::string value;

  void SetValue(std::string next) {
    value = std::move(next);
    NoteMaterializedChange();
  }
};

class TestApplication : public ae::Obj {
  APPTRAVERSE_OBJECT(TestApplication, ae::Obj, 0)

 protected:
  TestApplication() = default;

 public:
  explicit TestApplication(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(runtime))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, runtime);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, runtime);
  }

  TestRuntimeNode::ptr runtime;
};

APPTRAVERSE_REGISTER(TestRuntimeNode);
APPTRAVERSE_REGISTER(TestApplication);

void RunObjectRootStructuralOnce() {
  (void)&g_apptraverse_registrar_TestRuntimeNode;
  (void)&g_apptraverse_registrar_TestApplication;
  ae::RamDomainStorage model_storage;
  ae::RamDomainStorage ui_storage;
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};

  auto ui_app = TestApplication::ptr::Create(ae::CreateWith{ui_domain}.with_id(ae::ObjId{1}));
  auto model_app = TestApplication::ptr::Create(ae::CreateWith{model_domain}.with_id(ae::ObjId{1}));

  auto ui_runtime = TestRuntimeNode::ptr::Create(ae::CreateWith{ui_domain}.with_id(ae::ObjId{2}));
  InitializeRuntimeNode(*ui_runtime, *ui_runtime);
  ui_app->runtime = ui_runtime;
  ui_runtime->value = "initial";

  auto model_runtime = TestRuntimeNode::ptr::Create(ae::CreateWith{model_domain}.with_id(ae::ObjId{2}));
  InitializeRuntimeNode(*model_runtime, *model_runtime);
  model_app->runtime = model_runtime;
  model_runtime->value = "initial";

  std::uint64_t const gen0 = model_runtime->Generation();
  model_runtime->SetValue("updated");
  CHECK(model_runtime->Generation() > gen0);

  CHECK(PublicationTargetIsNodeShell(*model_runtime));

  ByteSink sink;
  SerializeStructuralObjectPublication(*model_app, sink);
  CHECK(!sink.bytes.empty());

  ByteSource in{sink.bytes.data(), sink.bytes.size(), 0};
  ApplyStructuralObjectPublication(in, ui_domain, ui_storage);

  CHECK(ui_app->runtime.is_valid());
  CHECK(ui_app->runtime->value == "updated");
  CHECK(ui_app->runtime->Generation() == model_runtime->Generation());
  CHECK(!PublicationTargetIsNodeShell(*ui_app));
  CHECK(ui_app->runtime->journal.empty());
  CHECK(!ui_app->runtime->base.is_valid());
}

void RunNodeRootStructuralOnce() {
  ae::RamDomainStorage model_storage;
  ae::RamDomainStorage ui_storage;
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};

  auto ui_node = TestRuntimeNode::ptr::Create(ae::CreateWith{ui_domain}.with_id(ae::ObjId{10}));
  InitializeRuntimeNode(*ui_node);
  ui_node->value = "ui";

  auto model_node = TestRuntimeNode::ptr::Create(ae::CreateWith{model_domain}.with_id(ae::ObjId{10}));
  InitializeRuntimeNode(*model_node);
  model_node->value = "model";

  std::uint64_t const gen0 = model_node->Generation();
  model_node->SetValue("changed");
  CHECK(model_node->Generation() > gen0);

  ByteSink sink;
  SerializeStructuralNodePublication(*model_node, sink);
  ByteSource in{sink.bytes.data(), sink.bytes.size(), 0};
  ApplyStructuralPublication(in, ui_domain, ui_storage);

  CHECK(ui_node->value == "changed");
  CHECK(ui_node->Generation() == model_node->Generation());
  CHECK(ui_node->journal.empty());
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  for (int i = 0; i < 20; ++i) {
    apptraverse::test::RunObjectRootStructuralOnce();
  }
  apptraverse::test::RunNodeRootStructuralOnce();
  std::cerr << "structural_publication_test OK\n";
  return 0;
}
