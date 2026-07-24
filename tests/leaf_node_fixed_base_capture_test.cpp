#include <cstdlib>
#include <iostream>
#include <string>

#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/node.h"

namespace apptraverse::test {

class LeafNode : public apptraverse::Node {
  AE_OBJECT(LeafNode, Node, 0)

 protected:
  LeafNode() = default;

 public:
  explicit LeafNode(ae::ObjProp prop) : Node{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name))

  std::string name;

  void CaptureBaseStateForTest() { CaptureBaseState(*this); }
};

}  // namespace apptraverse::test

namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " (" << __FILE__ << ':'         \
                << __LINE__ << ")\n";                                        \
      return EXIT_FAILURE;                                                   \
    }                                                                        \
  } while (0)

}  // namespace

int main() {
  using apptraverse::test::LeafNode;

  ae::RamDomainStorage storage;

  ae::Domain domain1{ae::Now(), storage};

  LeafNode::ptr base =
      LeafNode::ptr::Create(ae::CreateWith{domain1}.with_id(1000));
  CHECK(static_cast<bool>(base));
  base->name = "Old base";
  CHECK(!base->base.is_valid());
  CHECK(base->journal.empty());

  LeafNode::ptr live =
      LeafNode::ptr::Create(ae::CreateWith{domain1}.with_id(100));
  CHECK(static_cast<bool>(live));
  live->name = "Alice";
  live->base = base;
  CHECK(live->journal.empty());
  CHECK(live->base.is_valid());
  CHECK(live->base.is_loaded());

  auto* base_before = live->base.Load().get();
  CHECK(base_before != nullptr);

  live->CaptureBaseStateForTest();

  CHECK(live.id().id() == 100);
  CHECK(live->name == "Alice");
  CHECK(live->base.id().id() == 1000);
  CHECK(live->base.is_loaded());
  CHECK(live->journal.empty());
  CHECK(live->base.Load().get() == base_before);

  auto* base_after = live->base.Load().as<LeafNode>();
  CHECK(base_after != nullptr);
  CHECK(base_after->name == "Alice");
  CHECK(!base_after->base.is_valid());
  CHECK(base_after->journal.empty());

  ae::Domain domain2{ae::Now(), storage};
  LeafNode::ptr loaded_base =
      LeafNode::ptr::Declare(ae::CreateWith{domain2}.with_id(1000));
  loaded_base.Load();
  CHECK(static_cast<bool>(loaded_base));
  CHECK(loaded_base.id().id() == 1000);
  CHECK(loaded_base->name == "Alice");
  CHECK(!loaded_base->base.is_valid());
  CHECK(loaded_base->journal.empty());

  live->name = "Alice Cooper";
  CHECK(live->base.id().id() == 1000);
  CHECK(live->journal.empty());

  live->CaptureBaseStateForTest();

  CHECK(live.id().id() == 100);
  CHECK(live->name == "Alice Cooper");
  CHECK(live->base.id().id() == 1000);
  CHECK(live->base.Load().get() == base_before);

  auto* base_after_second = live->base.Load().as<LeafNode>();
  CHECK(base_after_second != nullptr);
  CHECK(base_after_second->name == "Alice Cooper");
  CHECK(!base_after_second->base.is_valid());
  CHECK(base_after_second->journal.empty());

  ae::Domain domain3{ae::Now(), storage};
  LeafNode::ptr reloaded_base =
      LeafNode::ptr::Declare(ae::CreateWith{domain3}.with_id(1000));
  reloaded_base.Load();
  CHECK(static_cast<bool>(reloaded_base));
  CHECK(reloaded_base.id().id() == 1000);
  CHECK(reloaded_base->name == "Alice Cooper");
  CHECK(!reloaded_base->base.is_valid());
  CHECK(reloaded_base->journal.empty());

  live.Save();

  ae::Domain domain4{ae::Now(), storage};
  LeafNode::ptr loaded_live =
      LeafNode::ptr::Declare(ae::CreateWith{domain4}.with_id(100));
  loaded_live.Load();
  CHECK(static_cast<bool>(loaded_live));
  CHECK(loaded_live.id().id() == 100);
  CHECK(loaded_live->name == "Alice Cooper");
  CHECK(loaded_live->journal.empty());
  CHECK(loaded_live->base.is_valid());
  CHECK(loaded_live->base.is_loaded());
  CHECK(loaded_live->base.id().id() == 1000);

  auto* loaded_live_base = loaded_live->base.Load().as<LeafNode>();
  CHECK(loaded_live_base != nullptr);
  CHECK(loaded_live_base->name == "Alice Cooper");
  CHECK(!loaded_live_base->base.is_valid());
  CHECK(loaded_live_base->journal.empty());

  return EXIT_SUCCESS;
}
