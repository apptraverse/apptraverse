#include <cstdlib>
#include <iostream>
#include <string>

#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/node_for.h"

namespace apptraverse::test {

class LeafNode : public apptraverse::NodeFor<LeafNode> {
  AE_OBJECT(LeafNode, Node, 0)

 protected:
  LeafNode() = default;

 public:
  explicit LeafNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name))

  std::string name;

  void CaptureBaseStateForTest() { CaptureBaseState(); }
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
  base->name = "Uninitialized base";
  CHECK(!base->base.is_valid());
  CHECK(base->journal.empty());

  LeafNode::ptr live =
      LeafNode::ptr::Create(ae::CreateWith{domain1}.with_id(100));
  CHECK(static_cast<bool>(live));
  live->name = "Root";
  live->base = base;
  CHECK(live->journal.empty());
  CHECK(live->base.is_valid());
  CHECK(live->base.is_loaded());

  auto* base_before = live->base.Load().get();
  CHECK(base_before != nullptr);

  live->CaptureBaseStateForTest();

  CHECK(live.id().id() == 100);
  CHECK(live->name == "Root");
  CHECK(live->base.id().id() == 1000);
  CHECK(live->base.is_loaded());
  CHECK(live->journal.empty());
  CHECK(live->base.Load().get() == base_before);

  auto* base_after = live->base.Load().as<LeafNode>();
  CHECK(base_after != nullptr);
  CHECK(base_after->name == "Root");
  CHECK(!base_after->base.is_valid());
  CHECK(base_after->journal.empty());

  live.Save();

  ae::Domain domain2{ae::Now(), storage};
  LeafNode::ptr loaded_live =
      LeafNode::ptr::Declare(ae::CreateWith{domain2}.with_id(100));
  loaded_live.Load();
  CHECK(static_cast<bool>(loaded_live));
  CHECK(loaded_live.id().id() == 100);
  CHECK(loaded_live->name == "Root");
  CHECK(loaded_live->journal.empty());
  CHECK(loaded_live->base.is_valid());
  CHECK(loaded_live->base.is_loaded());
  CHECK(loaded_live->base.id().id() == 1000);

  auto* loaded_live_base = loaded_live->base.Load().as<LeafNode>();
  CHECK(loaded_live_base != nullptr);
  CHECK(loaded_live_base->name == "Root");
  CHECK(!loaded_live_base->base.is_valid());
  CHECK(loaded_live_base->journal.empty());

  return EXIT_SUCCESS;
}
