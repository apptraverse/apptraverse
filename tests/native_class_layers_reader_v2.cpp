#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/obj_id.h"
#include "aether-objects/ptr/ptr.h"

#include "apptraverse/object_macros.h"
#include "apptraverse/shared_network_graph.h"

namespace apptraverse::test::evolution {

class LayerBase : public ae::Obj {
  // LayerBase evolved to native Version 1
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::evolution::LayerBase", LayerBase,
                           ae::Obj, 1)
 protected:
  LayerBase() = default;

 public:
  explicit LayerBase(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(base_value), AE_MMBR(added_base_field))

  // Version 0 compatibility loader: reads original base field and supplies
  // documented default (999) for newly added base field.
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, base_value);
    added_base_field = 999;
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    dnv(base_, base_value, added_base_field);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    dnv(base_, base_value, added_base_field);
  }

  std::uint32_t base_value{0};
  std::uint32_t added_base_field{0};
};

class LayerDerived : public LayerBase {
  // LayerDerived REMAINS native Version 0 with unchanged own fields!
  // It did NOT need a version bump merely because its base evolved.
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::evolution::LayerDerived",
                           LayerDerived, LayerBase, 0)
 protected:
  LayerDerived() = default;

 public:
  explicit LayerDerived(ae::ObjProp prop) : LayerBase{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(derived_value))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, derived_value);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, derived_value);
  }

  std::uint32_t derived_value{0};
};

APPTRAVERSE_REGISTER(LayerBase);
APPTRAVERSE_REGISTER(LayerDerived);

}  // namespace apptraverse::test::evolution

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      return 1;                                                              \
    }                                                                        \
  } while (0)

int main(int argc, char* argv[]) {
  std::string in_path = "/tmp/native_class_layers_evolution.bin";
  if (argc > 1) {
    in_path = argv[1];
  }

  apptraverse::EnsureObjectRegistration();

  std::ifstream in(in_path, std::ios::binary);
  if (!in) {
    std::cerr << "Failed to open input file: " << in_path << '\n';
    return 1;
  }
  std::vector<std::uint8_t> payload((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
  in.close();

  ae::RamDomainStorage storage;
  if (!apptraverse::DeserializeObjectGraph(payload, storage)) {
    std::cerr << "DeserializeObjectGraph failed in reader_v2\n";
    return 1;
  }

  ae::Domain domain{storage};
  ae::DomainGraph graph{&domain};
  ae::ObjId target_id{9001};

  auto root = graph.LoadRoot(target_id);
  CHECK(root);
  CHECK(root->GetClassId() ==
        apptraverse::test::evolution::LayerDerived::kClassId);

  auto derived =
      ae::Ptr<apptraverse::test::evolution::LayerDerived>{root};
  CHECK(derived->base_value == 11);
  CHECK(derived->derived_value == 22);
  CHECK(derived->added_base_field == 999);

  std::cout << "native_class_layers_reader_v2: PASS\n";
  return 0;
}
