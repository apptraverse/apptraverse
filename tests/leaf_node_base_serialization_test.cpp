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
  base->name = "Root";
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

  live.Save();

  ae::Domain domain2{ae::Now(), storage};
  LeafNode::ptr loaded =
      LeafNode::ptr::Declare(ae::CreateWith{domain2}.with_id(100));
  CHECK(loaded.is_valid());
  CHECK(!loaded.is_loaded());

  loaded.Load();

  CHECK(static_cast<bool>(loaded));
  CHECK(loaded.is_loaded());
  CHECK(loaded.id().id() == 100);
  CHECK(loaded->name == "Root");
  CHECK(loaded->journal.empty());
  CHECK(loaded->base.is_valid());
  CHECK(loaded->base.is_loaded());
  CHECK(loaded->base.id().id() == 1000);
  CHECK(loaded->base->GetClassId() == LeafNode::kClassId);

  auto* loaded_base = loaded->base.Load().as<LeafNode>();
  CHECK(loaded_base != nullptr);
  CHECK(loaded_base->name == "Root");
  CHECK(loaded_base->journal.empty());
  CHECK(!loaded_base->base.is_valid());

  CHECK(loaded.Load().get() != loaded->base.Load().get());
  CHECK(live.Load().get() != loaded.Load().get());
  CHECK(base.Load().get() != loaded->base.Load().get());

  return EXIT_SUCCESS;
}
