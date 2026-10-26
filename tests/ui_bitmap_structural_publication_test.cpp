#include <cstdlib>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_bitmap_image.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (false)

ae::ObjId const kRootId{0x82100001u};
ae::ObjId const kBitmapId{0x82100002u};

class BitmapPublicationRoot : public NodeFor<BitmapPublicationRoot> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::BitmapStructuralRoot",
                           BitmapPublicationRoot, Node, 0)

 protected:
  BitmapPublicationRoot() = default;

 public:
  explicit BitmapPublicationRoot(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(bitmap))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, bitmap);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, bitmap);
  }

  ui::UiBitmapImage::ptr bitmap;
};

APPTRAVERSE_REGISTER(BitmapPublicationRoot);

void RunUiBitmapStructuralPublicationTest() {
  EnsureObjectRegistration();
  ui::EnsureExtendedUiPresenterRegistration();
  (void)&g_apptraverse_registrar_BitmapPublicationRoot;

  std::vector<std::uint8_t> const frame_a{
      255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 128, 128, 128, 255,
  };
  std::vector<std::uint8_t> const frame_b{
      1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
  };

  ae::RamDomainStorage model_storage;
  ae::Domain model_domain{model_storage};
  auto root = BitmapPublicationRoot::ptr::Create(
      ae::CreateWith{model_domain}.with_id(kRootId));
  InitializeRuntimeNode(*root);
  root->bitmap = ui::UiBitmapImage::ptr::Create(
      ae::CreateWith{model_domain}.with_id(kBitmapId));
  InitializeRuntimeNode(*root->bitmap, *root);
  root->bitmap->width = 2;
  root->bitmap->height = 2;
  root->bitmap->bgra_bytes = frame_a;

  ByteSink initial;
  SerializeInitialPublication(*root, initial);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  ByteSource initial_in;
  initial_in.data = initial.bytes.data();
  initial_in.size = initial.bytes.size();
  auto ui_root = LoadInitialPublication(initial_in, ui_domain, ui_storage);
  auto ui = BitmapPublicationRoot::ptr::MakeFromThis(
      static_cast<BitmapPublicationRoot*>(ui_root.get()));
  CHECK(ui->bitmap->bgra_bytes == frame_a);

  root->bitmap->bgra_bytes = frame_b;
  ByteSink structural;
  SerializeStructuralNodePublication(*root->bitmap, structural);
  ByteSource structural_in;
  structural_in.data = structural.bytes.data();
  structural_in.size = structural.bytes.size();
  ApplyStructuralPublication(structural_in, ui_domain, ui_storage);
  CHECK(ui->bitmap->bgra_bytes == frame_b);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunUiBitmapStructuralPublicationTest();
  std::cout << "ui_bitmap_structural_publication_test OK\n";
  return 0;
}
