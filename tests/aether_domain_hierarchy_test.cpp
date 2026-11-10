#include <cstdlib>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"
#include "aether-objects/obj/obj_ptr.h"

#include "apptraverse/object_macros.h"

namespace apptraverse::test {
namespace {

void Fail(char const* message) {
  std::cerr << "aether_domain_hierarchy_test FAIL: " << message << '\n';
  std::exit(1);
}

class HierarchyLeaf : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::HierarchyLeaf", HierarchyLeaf,
                           ae::Obj, 0)

 protected:
  HierarchyLeaf() = default;

 public:
  explicit HierarchyLeaf(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT()

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    (void)dnv;
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    (void)dnv;
  }
};

int RunHierarchyTests() {
  using apptraverse::test::HierarchyLeaf;

  ae::RamDomainStorage parent_storage;
  ae::RamDomainStorage child_storage;
  ae::Domain parent{parent_storage};
  ae::Domain child{child_storage};
  child.set_parent_domain(&parent);

  ae::ObjId const parent_id{101u};
  ae::ObjId const child_only_id{202u};
  ae::ObjId const shared_shadow_id{303u};

  HierarchyLeaf::ptr parent_handle = HierarchyLeaf::ptr::Create(
      ae::CreateWith{parent}.with_id(parent_id));
  ae::Ptr<HierarchyLeaf> parent_obj = parent_handle.Load();

  HierarchyLeaf::ptr child_shadow_handle = HierarchyLeaf::ptr::Create(
      ae::CreateWith{child}.with_id(shared_shadow_id));
  ae::Ptr<HierarchyLeaf> child_shadow = child_shadow_handle.Load();

  if (!parent.FindLocal(parent_id)) {
    Fail("parent FindLocal");
  }
  if (parent.FindLocal(child_only_id)) {
    Fail("parent should not see child-only id");
  }
  if (!child.Find(parent_id)) {
    Fail("child Find parent fallback");
  }
  if (!child.FindLocal(shared_shadow_id)) {
    Fail("child FindLocal shadow");
  }
  if (child.Find(shared_shadow_id).get() != child_shadow.get()) {
    Fail("local shadows parent with same id");
  }

  ae::RamDomainStorage sibling_storage;
  ae::Domain sibling{sibling_storage};
  sibling.set_parent_domain(&parent);
  (void)HierarchyLeaf::ptr::Create(
      ae::CreateWith{sibling}.with_id(child_only_id));
  if (parent.Find(child_only_id)) {
    Fail("parent must not search sibling");
  }

  ae::ObjPtr<HierarchyLeaf> loaded{&child, parent_id, ae::ObjFlags{}, parent_obj};
  if (loaded.id() != parent_id) {
    Fail("loaded ObjPtr id");
  }
  if (loaded.domain() != &parent) {
    Fail("loaded ObjPtr domain follows object");
  }
  loaded.Reset();
  if (loaded.is_loaded()) {
    Fail("Reset unloads");
  }
  if (loaded.id() != parent_id) {
    Fail("unloaded ObjPtr retains id after Reset");
  }
  if (loaded.domain() != &parent) {
    Fail("unloaded ObjPtr retains domain after Reset");
  }

  std::cout << "aether_domain_hierarchy_test OK\n";
  return 0;
}

}  // namespace

}  // namespace apptraverse::test

int main() { return apptraverse::test::RunHierarchyTests(); }
