#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/node_for.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_bitmap_image.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_edit_box.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_string.h"

#if defined(_WIN32)
#include "apptraverse/platform/windows/win_ui_presenters.h"
#endif

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

ae::ObjId const kRootId{0x70000000u};
ae::ObjId const kWindowId{0x70000001u};
ae::ObjId const kLocaleId{0x70000002u};
ae::ObjId const kTitleId{0x70000003u};
ae::ObjId const kPresenterId{0x70000004u};
ae::ObjId const kEditAId{0x70000005u};
ae::ObjId const kEditBId{0x70000006u};
ae::ObjId const kEditPresenterAId{0x70000007u};
ae::ObjId const kEditPresenterBId{0x70000008u};
ae::ObjId const kBitmapId{0x70000009u};
ae::ObjId const kBitmapPresenterId{0x7000000au};
ae::ObjId const kListId{0x7000000bu};
ae::ObjId const kListPresenterId{0x7000000cu};

std::vector<std::uint8_t> const kBitmapPixels{
    255, 0, 17, 255, 0, 128, 255, 64,
};

using DesktopPresenter = ui::UiDesktopWindowPresenter;

class PresenterGraphRoot : public NodeFor<PresenterGraphRoot> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::PresenterGraphRoot",
                           PresenterGraphRoot, Node, 0)

 protected:
  PresenterGraphRoot() = default;

 public:
  explicit PresenterGraphRoot(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window), AE_MMBR(window_presenter),
                    AE_MMBR(edit_presenter_a), AE_MMBR(edit_presenter_b),
                    AE_MMBR(bitmap_presenter), AE_MMBR(list_presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, window, window_presenter, edit_presenter_a, edit_presenter_b,
        bitmap_presenter, list_presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, window, window_presenter, edit_presenter_a, edit_presenter_b,
        bitmap_presenter, list_presenter);
  }

  ui::UiDesktopWindow::ptr window;
  DesktopPresenter::ptr window_presenter;
  ui::UiEditBoxPresenter::ptr edit_presenter_a;
  ui::UiEditBoxPresenter::ptr edit_presenter_b;
  ui::UiBitmapImagePresenter::ptr bitmap_presenter;
  ui::UiListContainerPresenter::ptr list_presenter;
};

APPTRAVERSE_REGISTER(PresenterGraphRoot);

struct BuiltGraph {
  PresenterGraphRoot::ptr root;
  ui::UiLocaleSettings::ptr locale;
  ui::UiLocalizationString::ptr title;
  ui::UiEditBox::ptr edit_a;
  ui::UiEditBox::ptr edit_b;
};

BuiltGraph BuildGraph(ae::Domain& domain) {
  BuiltGraph g;
  g.root = PresenterGraphRoot::ptr::Create(
      ae::CreateWith{domain}.with_id(kRootId));
  InitializeRuntimeNode(*g.root);

  g.root->window = ui::UiDesktopWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(kWindowId));
  InitializeRuntimeNode(*g.root->window, *g.root);

  g.locale = ui::UiLocaleSettings::ptr::Create(
      ae::CreateWith{domain}.with_id(kLocaleId));
  InitializeRuntimeNode(*g.locale, *g.root->window);
  g.title = ui::UiLocalizationString::ptr::Create(
      ae::CreateWith{domain}.with_id(kTitleId));
  InitializeRuntimeNode(*g.title, *g.root->window);
  auto en = ui::UiString::ptr::Create(ae::CreateWith{domain});
  en->utf8 = "Title EN";
  auto ru = ui::UiString::ptr::Create(ae::CreateWith{domain});
  ru->utf8 = "Title RU";
  g.title->strings = {en, ru};

  g.root->window_presenter = DesktopPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(kPresenterId));
  g.root->window_presenter->window = g.root->window;
  g.root->window_presenter->locale = g.locale;
  g.root->window_presenter->title = g.title;

  g.edit_a = ui::UiEditBox::ptr::Create(
      ae::CreateWith{domain}.with_id(kEditAId));
  InitializeRuntimeNode(*g.edit_a, *g.root->window);
  g.edit_b = ui::UiEditBox::ptr::Create(
      ae::CreateWith{domain}.with_id(kEditBId));
  InitializeRuntimeNode(*g.edit_b, *g.root->window);

  g.root->edit_presenter_a = ui::UiEditBoxPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(kEditPresenterAId));
  g.root->edit_presenter_a->edit_box = g.edit_a;
  g.root->edit_presenter_a->window_presenter = g.root->window_presenter;

  g.root->edit_presenter_b = ui::UiEditBoxPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(kEditPresenterBId));
  g.root->edit_presenter_b->edit_box = g.edit_b;
  g.root->edit_presenter_b->window_presenter = g.root->window_presenter;

  auto bitmap = ui::UiBitmapImage::ptr::Create(
      ae::CreateWith{domain}.with_id(kBitmapId));
  InitializeRuntimeNode(*bitmap, *g.root->window);
  bitmap->width = 2;
  bitmap->height = 1;
  bitmap->bgra_bytes = kBitmapPixels;
  g.root->bitmap_presenter = ui::UiBitmapImagePresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(kBitmapPresenterId));
  g.root->bitmap_presenter->image = bitmap;
  g.root->bitmap_presenter->window_presenter = g.root->window_presenter;
  bitmap->presenter = g.root->bitmap_presenter;

  auto list = ui::UiListContainer::ptr::Create(
      ae::CreateWith{domain}.with_id(kListId));
  InitializeRuntimeNode(*list, *g.root->window);
  g.root->list_presenter = ui::UiListContainerPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(kListPresenterId));
  g.root->list_presenter->container = list;
  g.root->list_presenter->window_presenter = g.root->window_presenter;
  g.root->list_presenter->notify_list_selection_change = true;

  return g;
}

void RunPresenterGraphPersistenceTest() {
  EnableNoninteractiveCrt();
  EnsureObjectRegistration();
  (void)&g_apptraverse_registrar_PresenterGraphRoot;

  ByteSink snapshot;
  {
    ae::RamDomainStorage storage;
    ae::Domain domain{storage};
    BuiltGraph const live = BuildGraph(domain);

    ae::ObjId const presenter_id_before = live.root->window_presenter.id();
    ae::ObjId const locale_id_before = live.locale.id();
    CHECK(&*live.root->window_presenter->window == &*live.root->window);
    CHECK(&*live.root->edit_presenter_a->window_presenter ==
          &*live.root->window_presenter);

    SerializePersistentModelSnapshot(*live.root, snapshot);
    CHECK(live.root->window_presenter.id() == presenter_id_before);
    CHECK(live.locale.id() == locale_id_before);
    CHECK(live.root->window_presenter.is_valid());
  }

  // Recreate the Domain after the saving Domain and its storage are gone.
  ae::RamDomainStorage load_storage;
  ae::Domain load_domain{load_storage};
  auto load_root = PresenterGraphRoot::ptr::Create(
      ae::CreateWith{load_domain}.with_id(kRootId));
  InitializeRuntimeNode(*load_root);
  ByteSource in;
  in.data = snapshot.bytes.data();
  in.size = snapshot.bytes.size();
  in.pos = 0;
  LoadPersistentModelSnapshot(in, load_domain, load_storage, *load_root);

  CHECK(load_root->window_presenter.is_valid());
  CHECK(&*load_root->window_presenter->window == &*load_root->window);
  CHECK(load_root->window_presenter->locale.is_valid());
  CHECK(load_root->window_presenter->locale->obj_id == kLocaleId);
  CHECK(load_root->window_presenter->title->strings[0]->utf8 == "Title EN");
  CHECK(&*load_root->edit_presenter_a->window_presenter ==
        &*load_root->window_presenter);
  CHECK(&*load_root->edit_presenter_b->window_presenter ==
        &*load_root->window_presenter);
  CHECK(&*load_root->edit_presenter_a->window_presenter->locale ==
        &*load_root->window_presenter->locale);

  CHECK(load_root->bitmap_presenter.id() == kBitmapPresenterId);
  auto const bitmap = load_root->bitmap_presenter->image;
  CHECK(bitmap.id() == kBitmapId);
  CHECK(bitmap->width == 2);
  CHECK(bitmap->height == 1);
  CHECK(bitmap->bgra_bytes == kBitmapPixels);
  CHECK(&*bitmap->presenter == &*load_root->bitmap_presenter);
  CHECK(&*load_root->bitmap_presenter->window_presenter ==
        &*load_root->window_presenter);
  CHECK(load_root->list_presenter.id() == kListPresenterId);
  CHECK(load_root->list_presenter->container.id() == kListId);
  CHECK(load_root->list_presenter->notify_list_selection_change);
  CHECK(&*load_root->list_presenter->window_presenter ==
        &*load_root->window_presenter);

  for (auto const& [id, object_state] : load_storage.state) {
    CHECK(object_state.has_value());
    for (auto const& [class_id, versions] : *object_state) {
      CHECK(versions.size() == 1);
      CHECK(versions.begin()->first == 0);
    }
  }

  CHECK(!load_root->window_presenter->presentation_loaded);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunPresenterGraphPersistenceTest();
  std::cout << "presenter_graph_persistence_test OK\n";
  return 0;
}
