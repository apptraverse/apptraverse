#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/node_for.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/distill.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_desktop_window.h"
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
ae::ObjId const kListId{0x7000000bu};
ae::ObjId const kListPresenterId{0x7000000cu};

using DesktopPresenter = ui::UiDesktopWindowPresenter;

class PresenterGraphRoot : public NodeFor<PresenterGraphRoot> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::PresenterGraphRoot",
                           PresenterGraphRoot, Node, 0)

 protected:
  PresenterGraphRoot() = default;

 public:
  explicit PresenterGraphRoot(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window), AE_MMBR(window_presenter),
                    AE_MMBR(list_presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, window, window_presenter, list_presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, window, window_presenter, list_presenter);
  }

  ui::UiDesktopWindow::ptr window;
  DesktopPresenter::ptr window_presenter;
  ui::UiListContainerPresenter::ptr list_presenter;
};

APPTRAVERSE_REGISTER(PresenterGraphRoot);

struct BuiltGraph {
  PresenterGraphRoot::ptr root;
  ui::UiLocaleSettings::ptr locale;
  ui::UiLocalizationString::ptr title;
};

BuiltGraph BuildGraph(ae::Domain& domain) {
  BuiltGraph g;
  g.root = PresenterGraphRoot::ptr::Create(
      ae::CreateWith{domain}.with_id(kRootId));
  InitializeRuntimeNode(*g.root);

  g.root->window = ui::UiDesktopWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(kWindowId));
  g.root->window->BootstrapFrame(100, 100, 800, 600, apptraverse::ui::UiDesktopWindow::InitialPlacement::Explicit);
  InitializeRuntimeNode(*g.root->window, *g.root);

  g.locale = ui::UiLocaleSettings::ptr::Create(
      ae::CreateWith{domain}.with_id(kLocaleId));
  g.locale->BootstrapLanguageIndex(0);
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

  auto list = ui::UiListContainer::ptr::Create(
      ae::CreateWith{domain}.with_id(kListId));
  list->selection_index = -1;
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
  ui::EnsureUiPresenterRegistration();
  (void)&g_apptraverse_registrar_PresenterGraphRoot;

  ae::RamDomainStorage storage;

  {
    ae::Domain domain{storage};
    BuiltGraph const live = BuildGraph(domain);

    ae::ObjId const presenter_id_before = live.root->window_presenter.id();
    ae::ObjId const locale_id_before = live.locale.id();
    CHECK(&*live.root->window_presenter->window == &*live.root->window);

    ae::DomainGraph save_graph{&domain};
    ae::Ptr<ae::Obj> save_root = domain.Find(kRootId);
    CHECK(save_root);
    CHECK(save_graph.SaveRoot(save_root, kRootId).IsOk());
    CHECK(live.root->window_presenter.id() == presenter_id_before);
    CHECK(live.locale.id() == locale_id_before);
    CHECK(live.root->window_presenter.is_valid());
  }

  ae::Domain load_domain{storage};
  ae::DomainGraph load_graph{&load_domain};
  ae::Ptr<ae::Obj> loaded_obj;
  CHECK(load_graph.LoadRoot(kRootId, loaded_obj).IsOk());
  ae::Ptr<PresenterGraphRoot> load_root{loaded_obj};

  CHECK(load_root->window_presenter.is_valid());
  CHECK(&*load_root->window_presenter->window == &*load_root->window);
  CHECK(load_root->window_presenter->locale.is_valid());
  CHECK(load_root->window_presenter->locale->obj_id == kLocaleId);
  CHECK(load_root->window_presenter->title->strings[0]->utf8 == "Title EN");
  CHECK(load_root->list_presenter.id() == kListPresenterId);
  CHECK(load_root->list_presenter->container.id() == kListId);
  CHECK(load_root->list_presenter->notify_list_selection_change);
  CHECK(&*load_root->list_presenter->window_presenter ==
        &*load_root->window_presenter);

  for (auto const& [id, object_state] : storage.state) {
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
