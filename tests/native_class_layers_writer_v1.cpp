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
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::evolution::LayerBase", LayerBase,
                           ae::Obj, 0)
 protected:
  LayerBase() = default;

 public:
  explicit LayerBase(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(base_value))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, base_value);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, base_value);
  }

  std::uint32_t base_value{0};
};

class LayerDerived : public LayerBase {
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

int main(int argc, char* argv[]) {
  std::string out_path = "/tmp/native_class_layers_evolution.bin";
  if (argc > 1) {
    out_path = argv[1];
  }

  apptraverse::EnsureObjectRegistration();

  ae::RamDomainStorage storage;
  ae::ObjId target_id{9001};

  {
    ae::Domain domain{storage};
    auto derived_ptr = ae::MakePtr<apptraverse::test::evolution::LayerDerived>();
    domain.AddObject(target_id, derived_ptr);
    derived_ptr->domain = &domain;
    derived_ptr->obj_id = target_id;
    derived_ptr->base_value = 11;
    derived_ptr->derived_value = 22;

    ae::DomainGraph graph{&domain};
    graph.SaveRoot(derived_ptr, target_id);
  }

  std::vector<std::uint8_t> payload;
  if (!apptraverse::SerializeObjectGraph(storage, payload)) {
    std::cerr << "SerializeObjectGraph failed in writer_v1\n";
    return 1;
  }

  std::ofstream out(out_path, std::ios::binary);
  if (!out) {
    std::cerr << "Failed to open output file: " << out_path << '\n';
    return 1;
  }
  out.write(reinterpret_cast<char const*>(payload.data()),
            static_cast<std::streamsize>(payload.size()));
  out.close();

  std::cout << "native_class_layers_writer_v1 wrote " << payload.size()
            << " bytes to " << out_path << '\n';
  return 0;
}
