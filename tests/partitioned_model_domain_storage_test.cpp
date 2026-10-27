#include <cstdlib>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/partitioned_model_domain_storage.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse::test {
namespace {

class PartitionProbeNode : public NodeFor<PartitionProbeNode> {
  APPTRAVERSE_OBJECT(PartitionProbeNode, Node, 0)

 protected:
  PartitionProbeNode() = default;

 public:
  explicit PartitionProbeNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT()

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_);
  }
};

APPTRAVERSE_REGISTER(PartitionProbeNode);

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << "\n";                           \
      std::exit(1);                                                          \
    }                                                                        \
  } while (false)

bool RamHasClass(ae::RamDomainStorage const& ram, ae::ObjId id,
                 std::uint32_t class_id) {
  auto it = ram.state.find(id);
  if (it == ram.state.end() || !it->second) {
    return false;
  }
  return it->second->count(class_id) != 0;
}

}  // namespace

void RunPartitionedModelDomainStorageTest() {
  ae::RamDomainStorage durable;
  PartitionedModelDomainStorage storage{durable};
  ae::Domain domain{storage};
  auto runtime = PartitionProbeNode::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*runtime);

  ae::DomainQuery const runtime_query{runtime->obj_id, PartitionProbeNode::kClassId,
                                      0};
  {
    auto writer = storage.Store(runtime_query);
    CHECK(writer != nullptr);
    std::uint8_t byte = 42;
    (void)writer->Write(ae::seri::DataWriteTag{&byte, 1});
  }

  CHECK(RamHasClass(durable, runtime->obj_id, PartitionProbeNode::kClassId));
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::RunPartitionedModelDomainStorageTest();
  std::cout << "partitioned_model_domain_storage_test OK\n";
  return 0;
}
