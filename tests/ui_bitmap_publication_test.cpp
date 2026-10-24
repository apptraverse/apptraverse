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
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (false)

ae::ObjId const kRootId{0x82000001u};
ae::ObjId const kBitmapId{0x82000002u};

class BitmapPublicationRoot : public NodeFor<BitmapPublicationRoot> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::BitmapPublicationRoot",
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

void RunUiBitmapPublicationTest() {
  EnsureObjectRegistration();
  ui::EnsureExtendedUiPresenterRegistration();
  (void)&g_apptraverse_registrar_BitmapPublicationRoot;

  std::vector<std::uint8_t> const pixels{
      255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 128, 128, 128, 255,
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
  root->bitmap->bgra_bytes = pixels;

  ByteSink sink;
  SerializeInitialPublication(*root, sink);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  ByteSource in;
  in.data = sink.bytes.data();
  in.size = sink.bytes.size();
  auto ui_root = LoadInitialPublication(in, ui_domain, ui_storage);
  auto ui = BitmapPublicationRoot::ptr::MakeFromThis(
      static_cast<BitmapPublicationRoot*>(ui_root.get()));
  CHECK(ui->bitmap.is_valid());
  CHECK(ui->bitmap->width == 2);
  CHECK(ui->bitmap->height == 2);
  CHECK(ui->bitmap->bgra_bytes == pixels);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunUiBitmapPublicationTest();
  std::cout << "ui_bitmap_publication_test OK\n";
  return 0;
}
