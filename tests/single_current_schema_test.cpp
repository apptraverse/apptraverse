#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/node.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

namespace {

class SchemaProbeNode : public apptraverse::NodeFor<SchemaProbeNode> {
  APPTRAVERSE_OBJECT(SchemaProbeNode, apptraverse::Node, 0)

 protected:
  SchemaProbeNode() = default;

 public:
  explicit SchemaProbeNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, label);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, label);
  }

  std::string label;
};

APPTRAVERSE_REGISTER(SchemaProbeNode);

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << "\n";                           \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

}  // namespace

int main() {
  apptraverse::EnsureObjectRegistration();
  auto const tmp =
      std::filesystem::temp_directory_path() / "apptraverse_schema_zero_test";
  std::filesystem::remove_all(tmp);

  ae::ObjId::Type const kId = 880001;
  {
    apptraverse::DirectoryDomainStorage storage{tmp};
    ae::Domain domain{storage};
    auto node = SchemaProbeNode::ptr::Create(ae::CreateWith{domain}.with_id(kId));
    node->label = "alpha";
    node.Save();
  }
  {
    apptraverse::DirectoryDomainStorage storage{tmp};
    ae::Domain domain{storage};
    auto node = SchemaProbeNode::ptr::Declare(ae::CreateWith{domain}.with_id(kId));
    node.Load();
    CHECK(node.is_loaded());
    CHECK(node->label == "alpha");
    node->label = "beta";
    node.Save();
  }
  {
    apptraverse::DirectoryDomainStorage storage{tmp};
    ae::Domain domain{storage};
    auto node = SchemaProbeNode::ptr::Declare(ae::CreateWith{domain}.with_id(kId));
    node.Load();
    CHECK(node->label == "beta");
  }

  std::filesystem::remove_all(tmp);
  std::cout << "single_current_schema_test OK\n";
  return 0;
}
