#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/presenter.h"

#include "main_window_ids.h"
#include "main_window_lifecycle.h"
#include "main_window_model.h"

namespace apptraverse::test {

class TestMainWindowPresenter : public MainWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::TestMainWindowPresenter",
                           TestMainWindowPresenter, MainWindowPresenter, 0)

 protected:
  TestMainWindowPresenter() = default;

 public:
  explicit TestMainWindowPresenter(ae::ObjProp prop)
      : MainWindowPresenter{prop} {}

  ~TestMainWindowPresenter() override { ++dtor_calls; }

  AE_OBJECT_REFLECT()

  void OnLoad() override { ++on_load_calls; }
  void OnUnload() override { ++on_unload_calls; }

  static inline std::atomic<int> on_load_calls{0};
  static inline std::atomic<int> on_unload_calls{0};
  static inline std::atomic<int> dtor_calls{0};
};

namespace {

APPTRAVERSE_REGISTER(TestMainWindowPresenter);

}  // namespace

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"     \
                << __LINE__ << '\n';                                        \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

std::filesystem::path TestDir(char const* name) {
  auto path = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(path);
  return path;
}

void WaitPublished(ModelSession& session) {
  std::unique_lock<std::mutex> lock{session.mu};
  CHECK(session.cv.wait_for(lock, std::chrono::seconds{30}, [&] {
    return session.channel.has_unread_published();
  }));
}

void PersistFixture(std::filesystem::path const& dir) {
  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = BuildMainWindowGraph(domain);
  FinalizeDistilledGraph(*application);
  SaveDistilledRoot(*application);
}

Application::ptr LoadUiFromSession(ModelSession& session, ae::Domain& ui_domain,
                                    ae::IDomainStorage& ui_storage) {
  auto bytes = session.channel.TakePublishedCopy();
  CHECK(!bytes.empty());
  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  auto root = LoadInitialPublication(in, ui_domain, ui_storage);
  return Application::ptr::MakeFromThis(
      static_cast<Application*>(root.get()));
}

bool PersistedApplicationExists(std::filesystem::path const& dir) {
  if (!std::filesystem::exists(dir)) {
    return false;
  }
  DirectoryDomainStorage storage{dir};
  return !storage
              .Enumerate(ae::ObjId{main_window::ToObjId(
                  main_window::ObjId::Application)})
              .empty();
}

void TestLoadOnlyExistingState() {
  TestMainWindowPresenter::on_load_calls.store(0);
  TestMainWindowPresenter::dtor_calls.store(0);
  auto dir = TestDir("apptraverse_main_window_load_only");
  PersistFixture(dir);

  ModelSession session;
  session.state_dir = dir;
  std::thread model{[&] { session.Run(); }};
  WaitPublished(session);
  CHECK(PersistedApplicationExists(dir));

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadUiFromSession(session, ui_domain, ui_storage);
  CHECK(ui_app->obj_id.id() ==
        main_window::ToObjId(main_window::ObjId::Application));
  CHECK(ui_app->main_window->obj_id.id() ==
        main_window::ToObjId(main_window::ObjId::MainWindow));
  CHECK(ui_app->main_window->x == main_window::kDefaultX);
  CHECK(ui_app->main_window->presenter->GetClassId() ==
        TestMainWindowPresenter::kClassId);
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 0);

  session.RequestStop();
  model.join();
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 0);
  CHECK(TestMainWindowPresenter::dtor_calls.load() == 1);
  std::filesystem::remove_all(dir);
}

void TestLoadOnlyPresenterHooks() {
  TestMainWindowPresenter::on_load_calls.store(0);
  TestMainWindowPresenter::on_unload_calls.store(0);
  TestMainWindowPresenter::dtor_calls.store(0);
  auto dir = TestDir("apptraverse_main_window_load_only_presenter");
  PersistFixture(dir);

  ModelSession session;
  session.state_dir = dir;
  std::thread model{[&] { session.Run(); }};
  WaitPublished(session);
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 0);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadUiFromSession(session, ui_domain, ui_storage);
  InitializePresenters(*ui_app);
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 1);

  session.RequestStop();
  model.join();
  CHECK(TestMainWindowPresenter::dtor_calls.load() == 1);
  UnloadPresenters(*ui_app);
  CHECK(TestMainWindowPresenter::on_unload_calls.load() == 1);
  auto const app_id = ui_app->obj_id;
  auto const window_id = ui_app->main_window->obj_id;
  auto const presenter_id = ui_app->main_window->presenter->obj_id;
  ui_app = {};
  CHECK(!ui_domain.Find(app_id));
  CHECK(!ui_domain.Find(window_id));
  CHECK(!ui_domain.Find(presenter_id));
  CHECK(TestMainWindowPresenter::dtor_calls.load() == 2);
  std::filesystem::remove_all(dir);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestLoadOnlyExistingState();
  apptraverse::test::TestLoadOnlyPresenterHooks();
  std::cout << "main_window_lifecycle_load_only_test OK\n";
  return 0;
}
