#include <cstdlib>
#include <cstdint>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/presenter.h"
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

ae::ObjId const kAppId{0x81000001u};
ae::ObjId const kWindowId{0x81000002u};
ae::ObjId const kLocaleId{0x81000003u};
ae::ObjId const kPresenterId{0x81000004u};

class CycleApp;
class CycleWindow;
class CyclePresenter;

class CycleApp : public NodeFor<CycleApp> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::CycleApp", CycleApp, Node, 0)

 protected:
  CycleApp() = default;

 public:
  explicit CycleApp(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, window);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, window);
  }

  ae::ObjPtr<CycleWindow> window;
};

class CycleWindow : public NodeFor<CycleWindow> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::CycleWindow", CycleWindow, Node,
                           0)

 protected:
  CycleWindow() = default;

 public:
  explicit CycleWindow(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(app), AE_MMBR(locale), AE_MMBR(window_presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, app, locale, window_presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, app, locale, window_presenter);
  }

  ae::ObjPtr<CycleApp> app;
  ui::UiLocaleSettings::ptr locale;
  ae::ObjPtr<CyclePresenter> window_presenter;
};

class CyclePresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::CyclePresenter", CyclePresenter,
                           Presenter, 0)

 protected:
  CyclePresenter() = default;

 public:
  explicit CyclePresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window), AE_MMBR(locale))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(window, locale);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(window, locale);
  }

  ae::ObjPtr<CycleWindow> window;
  ui::UiLocaleSettings::ptr locale;
};

APPTRAVERSE_REGISTER(CycleApp);
APPTRAVERSE_REGISTER(CycleWindow);
APPTRAVERSE_REGISTER(CyclePresenter);

void RunCheckpointLoadCyclePersistenceTest() {
  EnsureObjectRegistration();
  (void)&g_apptraverse_registrar_CycleApp;
  (void)&g_apptraverse_registrar_CycleWindow;
  (void)&g_apptraverse_registrar_CyclePresenter;

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto app = CycleApp::ptr::Create(ae::CreateWith{domain}.with_id(kAppId));
  InitializeRuntimeNode(*app);
  auto window = CycleWindow::ptr::Create(ae::CreateWith{domain}.with_id(kWindowId));
  InitializeRuntimeNode(*window, *app);
  auto locale = ui::UiLocaleSettings::ptr::Create(
      ae::CreateWith{domain}.with_id(kLocaleId));
  InitializeRuntimeNode(*locale, *window);
  auto presenter = CyclePresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(kPresenterId));
  presenter->window = window;
  presenter->locale = locale;
  app->window = window;
  window->app = app;
  window->locale = locale;
  window->window_presenter = presenter;

  ByteSink snapshot;
  SerializePersistentModelSnapshot(*app, snapshot);

  storage = ae::RamDomainStorage{};
  ae::Domain load_domain{storage};
  auto load_app = CycleApp::ptr::Create(ae::CreateWith{load_domain}.with_id(kAppId));
  InitializeRuntimeNode(*load_app);
  ByteSource in;
  in.data = snapshot.bytes.data();
  in.size = snapshot.bytes.size();
  in.pos = 0;
  LoadPersistentModelSnapshot(in, load_domain, storage, *load_app);

  CHECK(load_app->window.is_valid());
  CHECK(&*load_app->window == &*load_domain.Find(kWindowId));
  CHECK(&*load_app->window->app == &*load_app);
  CHECK(load_app->window->locale.is_valid());
  CHECK(load_app->window->window_presenter.is_valid());
  CHECK(&*load_app->window->window_presenter->window == &*load_app->window);
  CHECK(&*load_app->window->window_presenter->locale ==
        &*load_app->window->locale);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunCheckpointLoadCyclePersistenceTest();
  std::cout << "checkpoint_load_cycle_persistence_test OK\n";
  return 0;
}
