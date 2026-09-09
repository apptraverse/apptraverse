#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/object_serialization.h"

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

  void OnLoad() override { ++on_load_calls; }

  void OnModelChanged() override {
    ++on_model_changed_calls;
    if (!WindowChangePublicationIsCurrent()) {
      return;
    }
    actual_x = window->x;
    actual_y = window->y;
    actual_width = window->width;
    actual_height = window->height;
  }

  std::int32_t actual_x{main_window::kDefaultX};
  std::int32_t actual_y{main_window::kDefaultY};
  std::int32_t actual_width{main_window::kDefaultWidth};
  std::int32_t actual_height{main_window::kDefaultHeight};

  static inline std::atomic<int> on_load_calls{0};
  static inline std::atomic<int> on_model_changed_calls{0};

  static void ResetCounts() {
    on_load_calls.store(0);
    on_model_changed_calls.store(0);
  }
};

namespace {

APPTRAVERSE_REGISTER(TestMainWindowPresenter);

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

std::vector<std::uint8_t> TakeAndWake(ModelSession& session) {
  std::vector<std::uint8_t> bytes;
  {
    std::lock_guard<std::mutex> lock{session.mu};
    bytes = session.channel.TakePublishedCopy();
  }
  session.cv.notify_all();
  CHECK(!bytes.empty());
  return bytes;
}

Application::ptr LoadInitialUi(std::vector<std::uint8_t> const& bytes,
                                ae::Domain& ui_domain,
                                ae::IDomainStorage& ui_storage) {
  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  auto root = LoadInitialPublication(in, ui_domain, ui_storage);
  return Application::ptr::MakeFromThis(static_cast<Application*>(root.get()));
}

using StateSnapshot = std::map<std::string, std::vector<std::uint8_t>>;

StateSnapshot SnapshotState(std::filesystem::path const& dir) {
  StateSnapshot files;
  if (!std::filesystem::exists(dir)) {
    return files;
  }
  for (auto const& entry :
       std::filesystem::recursive_directory_iterator{dir}) {
    if (!entry.is_regular_file()) {
      continue;
    }
    std::ifstream in{entry.path(), std::ios::binary};
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>{in},
                                    std::istreambuf_iterator<char>{}};
    files.emplace(std::filesystem::relative(entry.path(), dir).generic_string(),
                  std::move(bytes));
  }
  return files;
}

void CheckPersisted(std::filesystem::path const& dir, std::int32_t x,
                    std::int32_t y, std::int32_t width, std::int32_t height,
                    std::size_t journal_size) {
  DirectoryDomainStorage storage{dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{main_window::ToObjId(main_window::ObjId::Application)});
  CHECK(application->main_window->x == x);
  CHECK(application->main_window->y == y);
  CHECK(application->main_window->width == width);
  CHECK(application->main_window->height == height);
  CHECK(application->main_window->journal.size() == journal_size);
}

void TestCommandUpdatesExistingMirror() {
  TestMainWindowPresenter::ResetCounts();
  auto dir = TestDir("apptraverse_window_changed_command");
  ModelSession session;
  session.state_dir = dir;
  std::thread model{[&] {
    session.Run([&session](PublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);
  InitializePresenters(*ui_app);
  auto* app = &*ui_app;
  auto* window = &*ui_app->main_window;
  auto* presenter = &*ui_app->main_window->presenter;
  auto const generation = ui_app->main_window->Generation();
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 1);

  session.SubmitWindowChanged(WindowChangedCommand{1, 10, 20, 800, 600});
  WaitPublished(session);
  ApplyMainWindowIncremental(TakeAndWake(session), *ui_app, ui_storage);

  CHECK(&*ui_app == app);
  CHECK(&*ui_app->main_window == window);
  CHECK(&*ui_app->main_window->presenter == presenter);
  CHECK(ui_app->main_window->x == 10);
  CHECK(ui_app->main_window->y == 20);
  CHECK(ui_app->main_window->width == 800);
  CHECK(ui_app->main_window->height == 600);
  CHECK(ui_app->main_window->journal.empty());
  CHECK(!ui_app->main_window->base.is_valid());
  CHECK(ui_app->main_window->Generation() == generation + 1);
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 1);
  CHECK(TestMainWindowPresenter::on_model_changed_calls.load() == 1);
  CHECK(ui_app->main_window->presenter
            ->last_acknowledged_window_change_sequence == 1);
  CheckPersisted(dir, main_window::kDefaultX, main_window::kDefaultY,
                 main_window::kDefaultWidth, main_window::kDefaultHeight, 0);

  session.RequestStop();
  model.join();
  CheckPersisted(dir, 10, 20, 800, 600, 1);
  std::filesystem::remove_all(dir);
}

void TestCoalescePendingCommands() {
  auto dir = TestDir("apptraverse_window_changed_coalesce");
  ModelSession session;
  session.state_dir = dir;
  std::thread model{[&] {
    session.Run([&session](PublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);

  session.SubmitWindowChanged(WindowChangedCommand{1, 1, 1, 800, 600});
  session.SubmitWindowChanged(WindowChangedCommand{2, 2, 2, 900, 700});
  session.SubmitWindowChanged(WindowChangedCommand{3, 3, 3, 1000, 800});
  CHECK(session.channel.publish_count() == 1);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);
  WaitPublished(session);
  ApplyMainWindowIncremental(TakeAndWake(session), *ui_app, ui_storage);
  CHECK(ui_app->main_window->x == 3);
  CHECK(ui_app->main_window->y == 3);
  CHECK(ui_app->main_window->width == 1000);
  CHECK(ui_app->main_window->height == 800);
  CHECK(ui_app->main_window->presenter
            ->last_acknowledged_window_change_sequence == 3);
  CHECK(session.channel.publish_count() == 2);
  CheckPersisted(dir, main_window::kDefaultX, main_window::kDefaultY,
                 main_window::kDefaultWidth, main_window::kDefaultHeight, 0);

  session.RequestStop();
  model.join();
  CheckPersisted(dir, 3, 3, 1000, 800, 1);
  std::filesystem::remove_all(dir);
}

void TestNoOpCommand() {
  auto dir = TestDir("apptraverse_window_changed_noop");
  ModelSession session;
  session.state_dir = dir;
  std::thread model{[&] {
    session.Run([&session](PublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);
  auto initial = TakeAndWake(session);
  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadInitialUi(initial, ui_domain, ui_storage);
  auto const generation = ui_app->main_window->Generation();
  auto const before_command = SnapshotState(dir);

  session.SubmitWindowChanged(WindowChangedCommand{
      7, main_window::kDefaultX, main_window::kDefaultY,
      main_window::kDefaultWidth, main_window::kDefaultHeight});
  WaitPublished(session);
  ApplyMainWindowIncremental(TakeAndWake(session), *ui_app, ui_storage);
  CHECK(session.channel.publish_count() == 2);
  CHECK(ui_app->main_window->Generation() == generation);
  CHECK(ui_app->main_window->presenter
            ->last_acknowledged_window_change_sequence == 7);
  CHECK(SnapshotState(dir) == before_command);

  session.RequestStop();
  model.join();
  CheckPersisted(dir, main_window::kDefaultX, main_window::kDefaultY,
                 main_window::kDefaultWidth, main_window::kDefaultHeight, 0);
  std::filesystem::remove_all(dir);
}

void TestStopBeatsPendingChange() {
  auto dir = TestDir("apptraverse_window_changed_stop");
  ModelSession session;
  session.state_dir = dir;
  std::mutex gate_mu;
  std::condition_variable gate_cv;
  bool in_callback = false;
  bool release_callback = false;
  std::thread model{[&] {
    session.Run([&](PublicationKind) {
      {
        std::lock_guard<std::mutex> lock{gate_mu};
        in_callback = true;
      }
      gate_cv.notify_all();
      std::unique_lock<std::mutex> lock{gate_mu};
      CHECK(gate_cv.wait_for(lock, std::chrono::seconds{30},
                             [&] { return release_callback; }));
    });
  }};
  {
    std::unique_lock<std::mutex> lock{gate_mu};
    CHECK(gate_cv.wait_for(lock, std::chrono::seconds{30},
                            [&] { return in_callback; }));
  }
  (void)TakeAndWake(session);
  session.SubmitWindowChanged(WindowChangedCommand{1, 9, 9, 500, 400});
  session.RequestStop();
  {
    std::lock_guard<std::mutex> lock{gate_mu};
    release_callback = true;
  }
  gate_cv.notify_all();
  model.join();
  CHECK(session.channel.publish_count() == 1);
  CheckPersisted(dir, main_window::kDefaultX, main_window::kDefaultY,
                 main_window::kDefaultWidth, main_window::kDefaultHeight, 0);
  std::filesystem::remove_all(dir);
}

void TestRestartRestoresGeometry() {
  auto dir = TestDir("apptraverse_window_changed_restart");
  {
    ModelSession session;
    session.state_dir = dir;
    std::thread model{[&] {
      session.Run([&session](PublicationKind) { session.cv.notify_all(); });
    }};
    WaitPublished(session);
    ae::RamDomainStorage ui_storage;
    ae::Domain ui_domain{ui_storage};
    auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);
    session.SubmitWindowChanged(WindowChangedCommand{1, 40, 50, 700, 500});
    WaitPublished(session);
    ApplyMainWindowIncremental(TakeAndWake(session), *ui_app, ui_storage);
    CHECK(ui_app->main_window->width == 700);
    session.RequestStop();
    model.join();
  }
  {
    ModelSession session;
    session.state_dir = dir;
    std::thread model{[&] {
      session.Run([&session](PublicationKind) { session.cv.notify_all(); });
    }};
    WaitPublished(session);
    ae::RamDomainStorage ui_storage;
    ae::Domain ui_domain{ui_storage};
    auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);
    CHECK(ui_app->main_window->x == 40);
    CHECK(ui_app->main_window->y == 50);
    CHECK(ui_app->main_window->width == 700);
    CHECK(ui_app->main_window->height == 500);
    CHECK(ui_app->main_window->journal.empty());
    session.RequestStop();
    model.join();
  }
  std::filesystem::remove_all(dir);
}

void TestStalePublicationDoesNotRollBackNative() {
  TestMainWindowPresenter::ResetCounts();
  auto dir = TestDir("apptraverse_window_changed_stale");
  ModelSession session;
  session.state_dir = dir;
  std::thread model{[&] {
    session.Run([&session](PublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);
  InitializePresenters(*ui_app);
  auto* app = &*ui_app;
  auto* window = &*ui_app->main_window;
  auto* presenter = static_cast<TestMainWindowPresenter*>(
      &*ui_app->main_window->presenter);
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 1);

  presenter->last_submitted_window_change_sequence = 1;
  session.SubmitWindowChanged(WindowChangedCommand{1, 10, 20, 800, 600});
  WaitPublished(session);
  CHECK(session.channel.publish_count() == 2);

  presenter->last_submitted_window_change_sequence = 2;
  presenter->actual_x = 30;
  presenter->actual_y = 40;
  presenter->actual_width = 900;
  presenter->actual_height = 500;
  session.SubmitWindowChanged(WindowChangedCommand{2, 30, 40, 900, 500});

  ApplyMainWindowIncremental(TakeAndWake(session), *ui_app, ui_storage);
  CHECK(&*ui_app == app);
  CHECK(&*ui_app->main_window == window);
  CHECK(&*ui_app->main_window->presenter == presenter);
  CHECK(ui_app->main_window->x == 10);
  CHECK(ui_app->main_window->y == 20);
  CHECK(ui_app->main_window->width == 800);
  CHECK(ui_app->main_window->height == 600);
  CHECK(presenter->last_acknowledged_window_change_sequence == 1);
  CHECK(presenter->actual_x == 30);
  CHECK(presenter->actual_y == 40);
  CHECK(presenter->actual_width == 900);
  CHECK(presenter->actual_height == 500);
  CHECK(TestMainWindowPresenter::on_model_changed_calls.load() == 1);
  CHECK(TestMainWindowPresenter::on_load_calls.load() == 1);

  WaitPublished(session);
  ApplyMainWindowIncremental(TakeAndWake(session), *ui_app, ui_storage);
  CHECK(&*ui_app == app);
  CHECK(&*ui_app->main_window == window);
  CHECK(&*ui_app->main_window->presenter == presenter);
  CHECK(ui_app->main_window->x == 30);
  CHECK(ui_app->main_window->y == 40);
  CHECK(ui_app->main_window->width == 900);
  CHECK(ui_app->main_window->height == 500);
  CHECK(presenter->last_acknowledged_window_change_sequence == 2);
  CHECK(presenter->actual_x == 30);
  CHECK(presenter->actual_y == 40);
  CHECK(presenter->actual_width == 900);
  CHECK(presenter->actual_height == 500);
  CHECK(TestMainWindowPresenter::on_model_changed_calls.load() == 2);
  CheckPersisted(dir, main_window::kDefaultX, main_window::kDefaultY,
                 main_window::kDefaultWidth, main_window::kDefaultHeight, 0);

  session.RequestStop();
  model.join();
  CheckPersisted(dir, 30, 40, 900, 500, 2);
  std::filesystem::remove_all(dir);
}

void TestMultipleCommitsFlushOnceOnShutdown() {
  auto dir = TestDir("apptraverse_window_changed_multi_flush");
  ModelSession session;
  session.state_dir = dir;
  std::thread model{[&] {
    session.Run([&session](PublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  auto ui_app = LoadInitialUi(TakeAndWake(session), ui_domain, ui_storage);
  auto const before_commands = SnapshotState(dir);

  WindowChangedCommand const commands[] = {
      {1, 11, 21, 700, 500},
      {2, 12, 22, 710, 510},
      {3, 13, 23, 720, 520},
  };
  for (auto const& command : commands) {
    session.SubmitWindowChanged(command);
    WaitPublished(session);
    ApplyMainWindowIncremental(TakeAndWake(session), *ui_app, ui_storage);
    CHECK(ui_app->main_window->x == command.x);
    CHECK(ui_app->main_window->width == command.width);
    CHECK(SnapshotState(dir) == before_commands);
    CheckPersisted(dir, main_window::kDefaultX, main_window::kDefaultY,
                   main_window::kDefaultWidth, main_window::kDefaultHeight, 0);
  }

  session.RequestStop();
  model.join();
  CheckPersisted(dir, 13, 23, 720, 520, 3);
  std::filesystem::remove_all(dir);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestCommandUpdatesExistingMirror();
  apptraverse::test::TestCoalescePendingCommands();
  apptraverse::test::TestNoOpCommand();
  apptraverse::test::TestStopBeatsPendingChange();
  apptraverse::test::TestRestartRestoresGeometry();
  apptraverse::test::TestStalePublicationDoesNotRollBackNative();
  apptraverse::test::TestMultipleCommitsFlushOnceOnShutdown();
  std::cout << "main_window_window_changed_test OK\n";
  return 0;
}
