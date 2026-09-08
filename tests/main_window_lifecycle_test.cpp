#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/noninteractive_crt.h"
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
    last_window_id = window ? window->obj_id.id() : 0;
  }

  static inline std::atomic<int> on_load_calls{0};
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
  session.cv.wait(lock, [&] {
    return session.published.load() || session.finished.load();
  });
}

void WaitStage(ModelSession& session, ModelStartupStage stage) {
  std::unique_lock<std::mutex> lock{session.mu};
  session.cv.wait(lock, [&] {
    return session.stage.load() == static_cast<int>(stage) ||
           session.finished.load();
  });
}

void WaitFinished(ModelSession& session) {
  std::unique_lock<std::mutex> lock{session.mu};
  session.cv.wait(lock, [&] { return session.finished.load(); });
}

Application::ptr LoadUiFromSession(ModelSession& session,
                                    ae::Domain& ui_domain,
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

void TestFreshStartup() {
  auto dir = TestDir("apptraverse_main_window_fresh");
  ModelSession session;
  session.state_dir = dir;
  std::thread model{[&] { session.Run(); }};
  WaitPublished(session);
  CHECK(session.distilled_this_run.load());
  CHECK(session.published.load());
  CHECK(session.stage.load() == static_cast<int>(ModelStartupStage::Ready) ||
        session.stage.load() == static_cast<int>(ModelStartupStage::Serializing));
  CHECK(ApplicationStateExists(dir));
  session.RequestStop();
  WaitFinished(session);
  model.join();
  CHECK(session.finished.load());
  std::filesystem::remove_all(dir);
}

void TestExistingStartup() {
  auto dir = TestDir("apptraverse_main_window_existing");
  {
    ModelSession first;
    first.state_dir = dir;
    std::thread model{[&] { first.Run(); }};
    WaitPublished(first);
    CHECK(first.distilled_this_run.load());
    first.RequestStop();
    WaitFinished(first);
    model.join();
  }
  {
    ModelSession second;
    second.state_dir = dir;
    std::thread model{[&] { second.Run(); }};
    WaitPublished(second);
    CHECK(!second.distilled_this_run.load());
    CHECK(second.published.load());
    second.RequestStop();
    WaitFinished(second);
    model.join();
  }
  std::filesystem::remove_all(dir);
}

void TestInitialMirror() {
  auto dir = TestDir("apptraverse_main_window_mirror");
  ModelSession session;
  session.state_dir = dir;
  std::thread model{[&] { session.Run(); }};
  WaitPublished(session);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadUiFromSession(session, ui_domain, ui_storage);
  CHECK(ui_app->obj_id.id() == session.model_application_id.load());
  CHECK(ui_app->main_window);
  CHECK(ui_app->main_window->obj_id.id() == session.model_window_id.load());
  CHECK(reinterpret_cast<std::uintptr_t>(&*ui_app) !=
        session.model_application_addr.load());
  CHECK(reinterpret_cast<std::uintptr_t>(&*ui_app->main_window) !=
        session.model_window_addr.load());
  CHECK(reinterpret_cast<std::uintptr_t>(&ui_domain) !=
        session.model_domain_addr.load());
  CHECK(ui_app->main_window->x == session.model_window_x.load());
  CHECK(ui_app->main_window->y == session.model_window_y.load());
  CHECK(ui_app->main_window->width == session.model_window_width.load());
  CHECK(ui_app->main_window->height == session.model_window_height.load());
  CHECK(!ui_app->main_window->base.is_valid());
  CHECK(ui_app->main_window->journal.empty());
  CHECK(ui_app->main_window->presenter);
  CHECK(ui_app->main_window->presenter->obj_id.id() ==
        session.model_presenter_id.load());
  CHECK(ui_app->main_window->presenter->GetClassId() ==
        TestMainWindowPresenter::kClassId);
  CHECK(session.model_presenter_class_id.load() ==
        TestMainWindowPresenter::kClassId);
  CHECK(ui_app->main_window->presenter->window);
  CHECK(ui_app->main_window->presenter->window.id() ==
        ui_app->main_window->obj_id);
  CHECK(&*ui_app->main_window->presenter->window == &*ui_app->main_window);
  CHECK(&*ui_app->main_window->presenter !=
        reinterpret_cast<MainWindowPresenter*>(
            session.model_presenter_addr.load()));
  CHECK(!ui_app->main_window->presenter->presentation_initialized);
  CHECK(!session.model_presenter_initialized.load());

  session.RequestStop();
  WaitFinished(session);
  model.join();
  std::filesystem::remove_all(dir);
}

void TestMostDerivedFromNeutralPresenter() {
  TestMainWindowPresenter::on_load_calls.store(0);
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
  CHECK(ui_app);
  CHECK(ui_app->main_window->presenter);
  CHECK(ui_app->main_window->presenter->GetClassId() ==
        TestMainWindowPresenter::kClassId);
  CHECK(ui_app->obj_id == model_app->obj_id);
  CHECK(ui_app->main_window->obj_id == model_app->main_window->obj_id);
  CHECK(&*ui_app != &*model_app);
  CHECK(&*ui_app->main_window != &*model_app->main_window);
  CHECK(ui_app->domain != model_app->domain);
  CHECK(!ui_app->main_window->presenter->presentation_initialized);
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 0);
  CHECK(ui_app->main_window->presenter->window);
  CHECK(&*ui_app->main_window->presenter->window == &*ui_app->main_window);
  CHECK(&*ui_app->main_window->presenter->window->presenter ==
        &*ui_app->main_window->presenter);

  InitializePresenters(*ui_app);
  CHECK(ui_app->main_window->presenter->presentation_initialized);
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 1);
  CHECK(TestMainWindowPresenter::last_window_id.load() ==
        ui_app->main_window->obj_id.id());
  CHECK(!model_app->main_window->presenter->presentation_initialized);

  InitializePresenters(*ui_app);
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 1);

  auto const app_id = ui_app->obj_id;
  ui_app = {};
  ui_root = {};
  CHECK(!ui_domain.Find(app_id));
}

void TestPresenterMostDerivedAndInit() {
  TestMainWindowPresenter::on_load_calls.store(0);
  auto dir = TestDir("apptraverse_main_window_presenter");
  ModelSession session;
  session.state_dir = dir;
  std::thread model{[&] { session.Run(); }};
  WaitPublished(session);

  CHECK(session.model_presenter_id.load() != 0);
  CHECK(session.model_presenter_class_id.load() ==
        TestMainWindowPresenter::kClassId);
  CHECK(!session.model_presenter_initialized.load());
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 0);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadUiFromSession(session, ui_domain, ui_storage);
  CHECK(ui_app->main_window->presenter->GetClassId() ==
        TestMainWindowPresenter::kClassId);
  CHECK(!ui_app->main_window->presenter->presentation_initialized);
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 0);
  CHECK(ui_app->main_window->presenter->window);
  CHECK(&*ui_app->main_window->presenter->window == &*ui_app->main_window);
  CHECK(&*ui_app->main_window->presenter->window->presenter ==
        &*ui_app->main_window->presenter);

  InitializePresenters(*ui_app);
  CHECK(ui_app->main_window->presenter->presentation_initialized);
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 1);
  CHECK(TestMainWindowPresenter::last_window_id.load() ==
        ui_app->main_window->obj_id.id());
  CHECK(!session.model_presenter_initialized.load());

  InitializePresenters(*ui_app);
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 1);

  session.RequestStop();
  WaitFinished(session);
  model.join();
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 1);

  ui_app = {};
  CHECK(!ui_domain.Find(ae::ObjId{session.model_application_id.load()}));
  std::filesystem::remove_all(dir);
}

void TestThreadOwnership() {
  auto dir = TestDir("apptraverse_main_window_threads");
  ModelSession session;
  session.state_dir = dir;
  std::thread model{[&] { session.Run(); }};
  auto const consumer = std::this_thread::get_id();
  WaitPublished(session);
  CHECK(session.create_thread.load() == model.get_id());
  CHECK(session.create_thread.load() != consumer);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadUiFromSession(session, ui_domain, ui_storage);
  CHECK(std::this_thread::get_id() == consumer);
  CHECK(ui_app->domain == &ui_domain);

  session.RequestStop();
  WaitFinished(session);
  CHECK(session.destroy_thread.load() == model.get_id());
  CHECK(session.destroy_thread.load() != consumer);
  model.join();
  std::filesystem::remove_all(dir);
}

void TestStopWhileLoading() {
  auto dir = TestDir("apptraverse_main_window_stop_loading");
  ModelSession session;
  session.state_dir = dir;
  session.hold_stage.store(static_cast<int>(ModelStartupStage::Distilling));
  std::thread model{[&] { session.Run(); }};
  WaitStage(session, ModelStartupStage::Distilling);
  CHECK(!session.published.load());
  session.RequestStop();
  WaitFinished(session);
  model.join();
  CHECK(!session.published.load());
  CHECK(session.finished.load());
  CHECK(session.destroy_thread.load() == model.get_id() ||
        session.destroy_thread.load() != std::thread::id{});
  std::filesystem::remove_all(dir);
}

void TestStopAfterReady() {
  auto dir = TestDir("apptraverse_main_window_stop_ready");
  ModelSession session;
  session.state_dir = dir;
  std::thread model{[&] { session.Run(); }};
  WaitPublished(session);
  WaitStage(session, ModelStartupStage::Ready);
  session.RequestStop();
  WaitFinished(session);
  model.join();
  CHECK(session.finished.load());
  CHECK(session.destroy_thread.load() != std::thread::id{});
  std::filesystem::remove_all(dir);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::EnsureMainWindowRegistration();
  apptraverse::test::TestFreshStartup();
  apptraverse::test::TestExistingStartup();
  apptraverse::test::TestInitialMirror();
  apptraverse::test::TestMostDerivedFromNeutralPresenter();
  apptraverse::test::TestPresenterMostDerivedAndInit();
  apptraverse::test::TestThreadOwnership();
  apptraverse::test::TestStopWhileLoading();
  apptraverse::test::TestStopAfterReady();
  std::cout << "main_window_lifecycle_test OK\n";
  return 0;
}
