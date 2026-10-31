#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/distill.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/presenter.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_locale_settings.h"

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

ae::ObjId const kAppId{0x81100001u};
ae::ObjId const kWindowId{0x81100002u};
ae::ObjId const kLocaleId{0x81100003u};
ae::ObjId const kPresenterId{0x81100004u};

class RehydrationCycleApp;
class RehydrationCycleWindow;
class RehydrationCyclePresenter;

class RehydrationCycleApp : public NodeFor<RehydrationCycleApp> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::RehydrationCycleApp",
                           RehydrationCycleApp, Node, 0)

 protected:
  RehydrationCycleApp() = default;

 public:
  explicit RehydrationCycleApp(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, window);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, window);
  }

  ae::ObjPtr<RehydrationCycleWindow> window;
};

class RehydrationCycleWindow : public NodeFor<RehydrationCycleWindow> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::RehydrationCycleWindow",
                           RehydrationCycleWindow, Node, 0)

 protected:
  RehydrationCycleWindow() = default;

 public:
  explicit RehydrationCycleWindow(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(app), AE_MMBR(locale), AE_MMBR(window_presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, app, locale, window_presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, app, locale, window_presenter);
  }

  ae::ObjPtr<RehydrationCycleApp> app;
  ui::UiLocaleSettings::ptr locale;
  ae::ObjPtr<RehydrationCyclePresenter> window_presenter;
};

class RehydrationCyclePresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::RehydrationCyclePresenter",
                           RehydrationCyclePresenter, Presenter, 0)

 protected:
  RehydrationCyclePresenter() = default;

 public:
  explicit RehydrationCyclePresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window), AE_MMBR(locale))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(window, locale);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(window, locale);
  }

  ae::ObjPtr<RehydrationCycleWindow> window;
  ui::UiLocaleSettings::ptr locale;
};

APPTRAVERSE_REGISTER(RehydrationCycleApp);
APPTRAVERSE_REGISTER(RehydrationCycleWindow);
APPTRAVERSE_REGISTER(RehydrationCyclePresenter);

void CheckUniqueDomainObjects(ae::Domain& domain,
                              std::vector<ae::ObjId> const& ids) {
  std::unordered_map<std::uint32_t, ae::Obj*> seen;
  for (ae::ObjId const id : ids) {
    auto obj = domain.Find(id);
    CHECK(obj);
    auto [it, inserted] = seen.emplace(id.id(), obj.get());
    CHECK(inserted);
    CHECK(it->second == obj.get());
    auto again = domain.Find(id);
    CHECK(again.get() == obj.get());
  }
}

void RunCycleGraphLocaleRehydrationTest() {
  EnsureObjectRegistration();
  (void)&g_apptraverse_registrar_RehydrationCycleApp;
  (void)&g_apptraverse_registrar_RehydrationCycleWindow;
  (void)&g_apptraverse_registrar_RehydrationCyclePresenter;

  ae::RamDomainStorage storage;
  std::size_t locale_journal_before = 0;
  std::size_t window_journal_before = 0;

  {
    ae::Domain domain{storage};
    auto app = RehydrationCycleApp::ptr::Create(
        ae::CreateWith{domain}.with_id(kAppId));
    InitializeRuntimeNode(*app);
    auto window = RehydrationCycleWindow::ptr::Create(
        ae::CreateWith{domain}.with_id(kWindowId));
    InitializeRuntimeNode(*window, *app);
    auto locale = ui::UiLocaleSettings::ptr::Create(
        ae::CreateWith{domain}.with_id(kLocaleId));
    InitializeRuntimeNode(*locale, *window);
    auto presenter = RehydrationCyclePresenter::ptr::Create(
        ae::CreateWith{domain}.with_id(kPresenterId));
    presenter->window = window;
    presenter->locale = locale;
    app->window = window;
    window->app = app;
    window->locale = locale;
    window->window_presenter = presenter;

    auto lang = ui::SetUiLanguageIndexEvent::ptr::Create(ae::CreateWith{domain});
    lang->language_index = 1;
    locale->Commit(lang);
    CHECK(locale->language_index() == 1);

    locale_journal_before = locale->journal.size();
    std::uint64_t const locale_gen_before = locale->Generation();
    window_journal_before = window->journal.size();
    CHECK(window->base.is_valid());

    ae::DomainGraph save_graph{&domain};
    ae::Ptr<ae::Obj> save_root = domain.Find(kAppId);
    CHECK(save_root);
    CHECK(save_graph.SaveRoot(save_root, kAppId).IsOk());
    CHECK(locale->journal.size() == locale_journal_before);
    CHECK(locale->Generation() == locale_gen_before);
  }

  {
    ae::Domain load_domain{storage};
    ae::DomainGraph load_graph{&load_domain};
    ae::Ptr<ae::Obj> loaded_obj;
    CHECK(load_graph.LoadRoot(kAppId, loaded_obj).IsOk());
    ae::Ptr<RehydrationCycleApp> load_app{loaded_obj};

    std::vector<ae::ObjId> const ids{kAppId, kWindowId, kLocaleId,
                                     kPresenterId};
    CheckUniqueDomainObjects(load_domain, ids);

    CHECK(load_app->window->locale->language_index() == 1);
    CHECK(load_app->window->journal.size() == window_journal_before);
    CHECK(load_app->window->locale->journal.size() == locale_journal_before);
    CHECK(load_app->window->base.is_valid());
    CHECK(&*load_app->window->app == &*load_app);

    ae::Ptr<ae::Obj> const locale_cached = load_domain.Find(kLocaleId);
    CHECK(locale_cached.get() == &*load_app->window->locale);
    auto lang_unsaved =
        ui::SetUiLanguageIndexEvent::ptr::Create(ae::CreateWith{load_domain});
    lang_unsaved->language_index = 2;
    load_app->window->locale->Commit(lang_unsaved);
    CHECK(load_domain.Find(kLocaleId).get() == locale_cached.get());
    CHECK(load_app->window->locale->language_index() == 2);

    ae::Ptr<ae::Obj> reload_obj;
    CHECK(load_graph.LoadRoot(kLocaleId, reload_obj).IsOk());
    CHECK(reload_obj.get() == locale_cached.get());
    CHECK(static_cast<ui::UiLocaleSettings*>(reload_obj.get())->language_index() ==
          2);

    auto lang2 =
        ui::SetUiLanguageIndexEvent::ptr::Create(ae::CreateWith{load_domain});
    lang2->language_index = 0;
    load_app->window->locale->Commit(lang2);
    CHECK(load_app->window->locale->language_index() == 0);
  }
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunCycleGraphLocaleRehydrationTest();
  std::cout << "checkpoint_load_rehydration_test OK\n";
  return 0;
}
