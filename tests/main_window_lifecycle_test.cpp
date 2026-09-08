#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
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

  AE_OBJECT_REFLECT()

  void OnLoad() override {
    ++on_load_calls;
    last_window_id = window->obj_id.id();
  }

  void OnUnload() override { ++on_unload_calls; }

  static inline std::atomic<int> on_load_calls{0};
  static inline std::atomic<int> on_unload_calls{0};
  static inline std::atomic<std::uint32_t> last_window_id{0};
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
  session.cv.wait(lock, [&] { return session.channel.has_unread_published(); });
}

Application::ptr LoadUiFromSession(ModelSession& session, ae::Domain& ui_domain,
                                    ae::IDomainStorage& ui_storage) {
  auto bytes = session.channel.TakePublishedCopy();
  CHECK(!bytes.empty());
  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  auto root = LoadInitialPublication(in, ui_domain, ui_storage);
  auto application =
      Application::ptr::MakeFromThis(static_cast<Application*>(root.get()));
  CHECK(application);
  return application;
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

void TestDevStartup() {
  TestMainWindowPresenter::on_load_calls.store(0);
  auto dir = TestDir("apptraverse_main_window_dev");
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
  CHECK(ui_app->main_window->y == main_window::kDefaultY);
  CHECK(ui_app->main_window->width == main_window::kDefaultWidth);
  CHECK(ui_app->main_window->height == main_window::kDefaultHeight);
  CHECK(!ui_app->main_window->base.is_valid());
  CHECK(ui_app->main_window->journal.empty());
  CHECK(ui_app->main_window->presenter->GetClassId() ==
        TestMainWindowPresenter::kClassId);
  CHECK(&*ui_app->main_window->presenter->window == &*ui_app->main_window);
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 0);

  session.RequestStop();
  model.join();
  std::filesystem::remove_all(dir);
}

void TestDevReloadSameIds() {
  auto dir = TestDir("apptraverse_main_window_dev_reload");
  {
    ModelSession first;
    first.state_dir = dir;
    std::thread model{[&] { first.Run(); }};
    WaitPublished(first);
    CHECK(PersistedApplicationExists(dir));
    first.RequestStop();
    model.join();
  }
  {
    ModelSession second;
    second.state_dir = dir;
    std::thread model{[&] { second.Run(); }};
    WaitPublished(second);
    ae::RamDomainStorage ui_storage;
    ae::Domain ui_domain{ui_storage};
    auto ui_app = LoadUiFromSession(second, ui_domain, ui_storage);
    CHECK(ui_app->obj_id.id() ==
          main_window::ToObjId(main_window::ObjId::Application));
    CHECK(ui_app->main_window->obj_id.id() ==
          main_window::ToObjId(main_window::ObjId::MainWindow));
    second.RequestStop();
    model.join();
  }
  std::filesystem::remove_all(dir);
}

void TestMostDerivedFromNeutralPresenter() {
  TestMainWindowPresenter::on_load_calls.store(0);
  TestMainWindowPresenter::on_unload_calls.store(0);
  ae::RamDomainStorage model_storage;
  ae::Domain model_domain{model_storage};
  auto model_app = BuildMainWindowGraph(model_domain);
  CHECK(model_app->main_window->presenter->GetClassId() ==
        MainWindowPresenter::kClassId);
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 0);

  ByteSink sink;
  SerializeInitialPublication(*model_app, sink);
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 0);

  ByteSource in;
  in.data = sink.bytes.data();
  in.size = sink.bytes.size();
  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_root = LoadInitialPublication(in, ui_domain, ui_storage);
  auto ui_app =
      Application::ptr::MakeFromThis(static_cast<Application*>(ui_root.get()));
  CHECK(ui_app->main_window->presenter->GetClassId() ==
        TestMainWindowPresenter::kClassId);
  CHECK(&*ui_app != &*model_app);
  CHECK(ui_app->domain != model_app->domain);
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 0);

  InitializePresenters(*ui_app);
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 1);
  CHECK(TestMainWindowPresenter::last_window_id.load() ==
        ui_app->main_window->obj_id.id());

  UnloadPresenters(*ui_app);
  CHECK(TestMainWindowPresenter::on_unload_calls.load() == 1);

  auto const app_id = ui_app->obj_id;
  ui_app = {};
  ui_root = {};
  CHECK(!ui_domain.Find(app_id));
  CHECK(TestMainWindowPresenter::on_unload_calls.load() == 1);
}

void TestPresenterInitFromPublishedGraph() {
  TestMainWindowPresenter::on_load_calls.store(0);
  TestMainWindowPresenter::on_unload_calls.store(0);
  auto dir = TestDir("apptraverse_main_window_presenter");
  ModelSession session;
  session.state_dir = dir;
  std::thread model{[&] { session.Run(); }};
  WaitPublished(session);
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 0);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadUiFromSession(session, ui_domain, ui_storage);
  CHECK(ui_app->main_window->presenter->GetClassId() ==
        TestMainWindowPresenter::kClassId);
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 0);

  InitializePresenters(*ui_app);
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 1);

  session.RequestStop();
  model.join();
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 1);
  CHECK(TestMainWindowPresenter::on_unload_calls.load() == 0);

  UnloadPresenters(*ui_app);
  CHECK(TestMainWindowPresenter::on_unload_calls.load() == 1);
  ui_app = {};
  CHECK(!ui_domain.Find(
      ae::ObjId{main_window::ToObjId(main_window::ObjId::Application)}));
  std::filesystem::remove_all(dir);
}

void TestShutdownAfterPublish() {
  auto dir = TestDir("apptraverse_main_window_shutdown");
  ModelSession session;
  session.state_dir = dir;
  std::thread model{[&] { session.Run(); }};
  WaitPublished(session);
  session.RequestStop();
  model.join();
  std::filesystem::remove_all(dir);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestDevStartup();
  apptraverse::test::TestDevReloadSameIds();
  apptraverse::test::TestMostDerivedFromNeutralPresenter();
  apptraverse::test::TestPresenterInitFromPublishedGraph();
  apptraverse::test::TestShutdownAfterPublish();
  std::cout << "main_window_lifecycle_test OK\n";
  return 0;
}
