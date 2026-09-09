#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/presenter.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"     \
                << __LINE__ << '\n';                                        \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

class OrderRoot;
class OrderChild;

class OrderRootPresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::OrderRootPresenter",
                           OrderRootPresenter, Presenter, 0)

 protected:
  OrderRootPresenter() = default;

 public:
  explicit OrderRootPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(root))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, root);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, root);
  }

  void OnLoad() override {
    load_seq.push_back(tag);
    ++on_load;
  }
  void OnUnload() override {
    unload_seq.push_back(tag);
    ++on_unload;
  }

  ae::ObjPtr<OrderRoot> root;
  char tag{'?'};

  static inline std::vector<char> load_seq;
  static inline std::vector<char> unload_seq;
  static inline int on_load{0};
  static inline int on_unload{0};

  static void Reset() {
    load_seq.clear();
    unload_seq.clear();
    on_load = 0;
    on_unload = 0;
  }
};

class OrderChildPresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::OrderChildPresenter",
                           OrderChildPresenter, Presenter, 0)

 protected:
  OrderChildPresenter() = default;

 public:
  explicit OrderChildPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(child))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, child);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, child);
  }

  bool ReadyForPresentation() const override;

  void OnLoad() override {
    load_seq.push_back(tag);
    ++on_load;
  }
  void OnUnload() override {
    unload_seq.push_back(tag);
    ++on_unload;
  }

  ae::ObjPtr<OrderChild> child;
  char tag{'?'};

  static inline std::vector<char> load_seq;
  static inline std::vector<char> unload_seq;
  static inline int on_load{0};
  static inline int on_unload{0};

  static void Reset() {
    load_seq.clear();
    unload_seq.clear();
    on_load = 0;
    on_unload = 0;
  }
};

class OrderChild : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::OrderChild", OrderChild, ae::Obj,
                           0)

 protected:
  OrderChild() = default;

 public:
  explicit OrderChild(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(root), AE_MMBR(presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, root, presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, root, presenter);
  }

  ae::ObjPtr<OrderRoot> root;
  OrderChildPresenter::ptr presenter;
};

class OrderRoot : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::OrderRoot", OrderRoot, ae::Obj, 0)

 protected:
  OrderRoot() = default;

 public:
  explicit OrderRoot(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(child), AE_MMBR(presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, child, presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, child, presenter);
  }

  OrderChild::ptr child;
  OrderRootPresenter::ptr presenter;
};

bool OrderChildPresenter::ReadyForPresentation() const {
  return child.is_valid() && child.is_loaded() && child->root.is_valid() &&
         child->root.is_loaded() && child->root->presenter.is_valid() &&
         child->root->presenter.is_loaded() &&
         child->root->presenter->presentation_loaded;
}

APPTRAVERSE_REGISTER(OrderRoot);
APPTRAVERSE_REGISTER(OrderChild);
APPTRAVERSE_REGISTER(OrderRootPresenter);
APPTRAVERSE_REGISTER(OrderChildPresenter);

struct Pair {
  OrderRoot::ptr root;
  OrderRootPresenter* root_p{};
  OrderChildPresenter* child_p{};
};

Pair MakePair(ae::Domain& domain, ae::ObjId::Type root_id,
              ae::ObjId::Type child_id, ae::ObjId::Type root_p_id,
              ae::ObjId::Type child_p_id, char root_tag, char child_tag) {
  auto root =
      OrderRoot::ptr::Create(ae::CreateWith{domain}.with_id(root_id));
  auto child =
      OrderChild::ptr::Create(ae::CreateWith{domain}.with_id(child_id));
  auto root_p = OrderRootPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(root_p_id));
  auto child_p = OrderChildPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(child_p_id));
  root->child = child;
  root->presenter = root_p;
  root_p->root = root;
  root_p->tag = root_tag;
  child->root = root;
  child->presenter = child_p;
  child_p->child = child;
  child_p->tag = child_tag;
  return Pair{root, &*root_p, &*child_p};
}

void TestLoadUnloadOrderIndependentOfObjId() {
  OrderRootPresenter::Reset();
  OrderChildPresenter::Reset();

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};

  // Child and child-presenter get LOWER ObjIds than the root so Save/map
  // order would prefer them first — lifecycle must still load parent first.
  auto a = MakePair(domain, 100, 10, 101, 11, 'A', 'a');
  auto b = MakePair(domain, 200, 20, 201, 21, 'B', 'b');

  // Forest: two independent roots held by a vector on a holder object.
  // Walk each root separately through InitializePresenters.
  InitializePresenters(*a.root);
  InitializePresenters(*b.root);

  CHECK(OrderRootPresenter::on_load == 2);
  CHECK(OrderChildPresenter::on_load == 2);
  CHECK(OrderRootPresenter::load_seq.size() == 2);
  CHECK(OrderChildPresenter::load_seq.size() == 2);
  // Within each pair, parent load_order < child load_order.
  CHECK(a.root_p->presentation_load_order < a.child_p->presentation_load_order);
  CHECK(b.root_p->presentation_load_order < b.child_p->presentation_load_order);

  UnloadPresenters(*a.root);
  CHECK(OrderChildPresenter::unload_seq.size() == 1);
  CHECK(OrderRootPresenter::unload_seq.size() == 1);
  CHECK(OrderChildPresenter::unload_seq[0] == 'a');
  CHECK(OrderRootPresenter::unload_seq[0] == 'A');
  CHECK(!a.root_p->presentation_loaded);
  CHECK(!a.child_p->presentation_loaded);

  UnloadPresenters(*b.root);
  CHECK(OrderChildPresenter::unload_seq.size() == 2);
  CHECK(OrderRootPresenter::unload_seq.size() == 2);
  CHECK(OrderChildPresenter::unload_seq[1] == 'b');
  CHECK(OrderRootPresenter::unload_seq[1] == 'B');
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestLoadUnloadOrderIndependentOfObjId();
  std::cout << "presenter_load_order_test OK\n";
  return 0;
}
