#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "aether-objects/obj/domain.h"

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/application_model_session.h"
#include "apptraverse/application_ui_session.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/node.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"
#include "apptraverse/ui/ui_presenters.h"

#include "minimal_app_graph_headless.h"
#include "minimal_application.h"

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

using example::minimal::BuildHeadlessMinimalApplicationGraph;
using example::minimal::EnsureHeadlessMinimalApplicationGraphRegistration;
using example::minimal::MinimalApplication;
using example::minimal::MinimalApplicationRootId;
using example::minimal::MinimalMainWindowId;
using example::minimal::MinimalPresenterId;

constexpr std::int32_t kInitialX = 120;
constexpr std::int32_t kInitialY = 80;
constexpr std::int32_t kInitialW = 640;
constexpr std::int32_t kInitialH = 480;
constexpr std::int32_t kReportedX = 33;
constexpr std::int32_t kReportedY = 44;
constexpr std::int32_t kReportedW = 700;
constexpr std::int32_t kReportedH = 500;
constexpr std::int32_t kSecondSessionX = 50;
constexpr std::int32_t kSecondSessionY = 60;
constexpr std::int32_t kSecondSessionW = 720;
constexpr std::int32_t kSecondSessionH = 520;

std::size_t CountJournalEventsWithClass(Node const& node,
                                        std::uint32_t class_id) {
  std::size_t count = 0;
  for (auto const& entry : node.journal) {
    if (entry.event && entry.event->GetClassId() == class_id) {
      ++count;
    }
  }
  return count;
}

ui::UiDesktopWindow& UiMainWindow(MinimalApplication& application) {
  application.main_window.Load();
  CHECK(application.main_window.is_valid());
  return *application.main_window;
}

ui::UiDesktopWindowPresenter& UiMainPresenter(MinimalApplication& application) {
  ui::UiDesktopWindow& window = UiMainWindow(application);
  window.presenter.Load();
  CHECK(window.presenter.is_valid());
  return static_cast<ui::UiDesktopWindowPresenter&>(*window.presenter);
}

struct PublicationWaiter {
  std::mutex mu;
  std::condition_variable cv;
  int pending{0};
  ApplicationPublicationKind kind{ApplicationPublicationKind::Initial};
};

ApplicationPublicationKind WaitPublication(PublicationWaiter& waiter) {
  std::unique_lock<std::mutex> lock{waiter.mu};
  waiter.cv.wait(lock, [&] { return waiter.pending > 0; });
  --waiter.pending;
  return waiter.kind;
}

std::filesystem::path TestProfileDir() {
  auto const dir = std::filesystem::temp_directory_path() /
                   "apptraverse_test" / "minimal_app_headless_cold";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

void TestMinimalAppHeadlessSessionE2e() {
  EnsureObjectRegistration();
  EnsureHeadlessMinimalApplicationGraphRegistration();

  std::filesystem::path const profile_dir = TestProfileDir();
  {
    DirectoryDomainStorage bootstrap_storage{profile_dir};
    ae::Domain bootstrap_domain{bootstrap_storage};
    auto application = BuildHeadlessMinimalApplicationGraph(bootstrap_domain);
    FinalizeDistilledGraph(*application);
    SaveDistilledRoot(*application);
  }

  bool persist_save_failed = false;
  std::size_t native_frame_events_at_incremental = 0;

  // --- First model session: load, UI mirror, frame command, persist ---
  {
    DirectoryDomainStorage session_storage{profile_dir};
    ApplicationModelRuntime runtime;
    PublicationWaiter waiter;
    std::unique_ptr<ApplicationUiSession> ui_session;
    int session_result = 1;

    ModelObjectProxy model_proxy{[&runtime](ModelObjectProxy::ModelWork work) {
      runtime.Post(std::move(work));
    }};

    ApplicationModelSessionHooks<MinimalApplication> hooks;
    hooks.prepare = [](MinimalApplication& application) {
      ui::LoadPresentedWindow(application.main_window);
    };
    hooks.before_incremental = [&](MinimalApplication& application) {
      ui::UiDesktopWindow& window = UiMainWindow(application);
      CHECK(window.x() == kReportedX);
      CHECK(window.y() == kReportedY);
      CHECK(window.client_width() == kReportedW);
      CHECK(window.client_height() == kReportedH);
      native_frame_events_at_incremental = CountJournalEventsWithClass(
          window, ui::ReportUiDesktopWindowFrameFromNativeEvent::kClassId);
      CHECK(native_frame_events_at_incremental == 1);
    };
    hooks.persist = [&](MinimalApplication& application) -> int {
      session_storage.BeginSaveSession();
      ui::SavePresentedWindow(application.main_window);
      MinimalApplication::ptr::MakeFromThis(&application).Save();
      persist_save_failed = session_storage.SaveSessionHadFailure();
      return persist_save_failed ? 1 : 0;
    };

    std::thread model_thread{[&] {
      session_result = RunApplicationModelSession(
          session_storage, MinimalApplicationRootId(), runtime,
          [&](ApplicationPublicationKind kind) {
            {
              std::lock_guard<std::mutex> lock{waiter.mu};
              waiter.kind = kind;
              ++waiter.pending;
            }
            waiter.cv.notify_one();
          },
          hooks);
    }};

    CHECK(WaitPublication(waiter) == ApplicationPublicationKind::Initial);
    std::vector<std::uint8_t> initial_bytes = runtime.TakePublishedCopy();
    runtime.NotifyPublicationConsumed();
    ui_session = std::make_unique<ApplicationUiSession>(initial_bytes, nullptr,
                                                        model_proxy);
    MinimalApplication& ui_app =
        static_cast<MinimalApplication&>(ui_session->root());
    ui::UiDesktopWindow& ui_window = UiMainWindow(ui_app);
    CHECK(ui_window.obj_id == MinimalMainWindowId());
    CHECK(ui_window.presenter.id() == MinimalPresenterId());
    CHECK(ui_window.x() == kInitialX);
    CHECK(ui_window.y() == kInitialY);
    CHECK(ui_window.client_width() == kInitialW);
    CHECK(ui_window.client_height() == kInitialH);

    UiMainPresenter(ui_app).SubmitFrameFromNative(kReportedX, kReportedY,
                                                  kReportedW, kReportedH);

    CHECK(WaitPublication(waiter) == ApplicationPublicationKind::Incremental);
    std::vector<std::uint8_t> incremental_bytes = runtime.TakePublishedCopy();
    runtime.NotifyPublicationConsumed();
    CHECK(ui_window.x() == kInitialX);
    CHECK(ui_window.y() == kInitialY);
    CHECK(ui_window.client_width() == kInitialW);
    CHECK(ui_window.client_height() == kInitialH);

    ui_session->ApplyStructural(incremental_bytes);
    CHECK(ui_window.x() == kReportedX);
    CHECK(ui_window.y() == kReportedY);
    CHECK(ui_window.client_width() == kReportedW);
    CHECK(ui_window.client_height() == kReportedH);

    runtime.RequestStop();
    model_thread.join();
    ui_session.reset();

    CHECK(session_result == 0);
    CHECK(!persist_save_failed);
    CHECK(native_frame_events_at_incremental == 1);
  }

  // --- Second model session: cold LoadApplication only, new runtime/UI ---
  DirectoryDomainStorage cold_storage{profile_dir};
  ApplicationModelRuntime cold_runtime;
  PublicationWaiter cold_waiter;
  std::unique_ptr<ApplicationUiSession> cold_ui_session;
  int cold_session_result = 1;
  std::size_t cold_frame_events_before_submit = 0;

  ModelObjectProxy cold_proxy{
      [&cold_runtime](ModelObjectProxy::ModelWork work) {
        cold_runtime.Post(std::move(work));
      }};

  ApplicationModelSessionHooks<MinimalApplication> cold_hooks;
  cold_hooks.prepare = [&](MinimalApplication& application) {
    ui::LoadPresentedWindow(application.main_window);
    ui::UiDesktopWindow& window = UiMainWindow(application);
    CHECK(window.x() == kReportedX);
    CHECK(window.y() == kReportedY);
    CHECK(window.client_width() == kReportedW);
    CHECK(window.client_height() == kReportedH);
    CHECK(window.obj_id == MinimalMainWindowId());
    CHECK(window.presenter.id() == MinimalPresenterId());
    cold_frame_events_before_submit = CountJournalEventsWithClass(
        window, ui::ReportUiDesktopWindowFrameFromNativeEvent::kClassId);
  };
  cold_hooks.before_incremental = [&](MinimalApplication& application) {
    ui::UiDesktopWindow& window = UiMainWindow(application);
    CHECK(window.x() == kSecondSessionX);
    CHECK(window.y() == kSecondSessionY);
    CHECK(window.client_width() == kSecondSessionW);
    CHECK(window.client_height() == kSecondSessionH);
    CHECK(CountJournalEventsWithClass(
              window, ui::ReportUiDesktopWindowFrameFromNativeEvent::kClassId) ==
          cold_frame_events_before_submit + 1);
  };
  cold_hooks.persist = [&cold_storage](MinimalApplication& application) -> int {
    cold_storage.BeginSaveSession();
    ui::SavePresentedWindow(application.main_window);
    MinimalApplication::ptr::MakeFromThis(&application).Save();
    return cold_storage.SaveSessionHadFailure() ? 1 : 0;
  };

  std::thread cold_thread{[&] {
    cold_session_result = RunApplicationModelSession(
        cold_storage, MinimalApplicationRootId(), cold_runtime,
        [&](ApplicationPublicationKind kind) {
          {
            std::lock_guard<std::mutex> lock{cold_waiter.mu};
            cold_waiter.kind = kind;
            ++cold_waiter.pending;
          }
          cold_waiter.cv.notify_one();
        },
        cold_hooks);
  }};

  CHECK(WaitPublication(cold_waiter) == ApplicationPublicationKind::Initial);
  std::vector<std::uint8_t> cold_initial_bytes =
      cold_runtime.TakePublishedCopy();
  cold_runtime.NotifyPublicationConsumed();
  cold_ui_session = std::make_unique<ApplicationUiSession>(
      cold_initial_bytes, nullptr, cold_proxy);
  MinimalApplication& cold_ui_app =
      static_cast<MinimalApplication&>(cold_ui_session->root());
  ui::UiDesktopWindow& cold_ui_window = UiMainWindow(cold_ui_app);
  CHECK(cold_ui_window.obj_id == MinimalMainWindowId());
  CHECK(cold_ui_window.presenter.id() == MinimalPresenterId());
  CHECK(cold_ui_window.x() == kReportedX);
  CHECK(cold_ui_window.y() == kReportedY);
  CHECK(cold_ui_window.client_width() == kReportedW);
  CHECK(cold_ui_window.client_height() == kReportedH);

  UiMainPresenter(cold_ui_app)
      .SubmitFrameFromNative(kSecondSessionX, kSecondSessionY, kSecondSessionW,
                             kSecondSessionH);

  CHECK(WaitPublication(cold_waiter) ==
        ApplicationPublicationKind::Incremental);
  std::vector<std::uint8_t> cold_incremental_bytes =
      cold_runtime.TakePublishedCopy();
  cold_runtime.NotifyPublicationConsumed();
  CHECK(cold_ui_window.x() == kReportedX);
  CHECK(cold_ui_window.y() == kReportedY);
  CHECK(cold_ui_window.client_width() == kReportedW);
  CHECK(cold_ui_window.client_height() == kReportedH);

  cold_ui_session->ApplyStructural(cold_incremental_bytes);
  CHECK(cold_ui_window.x() == kSecondSessionX);
  CHECK(cold_ui_window.y() == kSecondSessionY);
  CHECK(cold_ui_window.client_width() == kSecondSessionW);
  CHECK(cold_ui_window.client_height() == kSecondSessionH);

  cold_runtime.RequestStop();
  cold_thread.join();
  cold_ui_session.reset();

  CHECK(cold_session_result == 0);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::test::TestMinimalAppHeadlessSessionE2e();
  std::cout << "minimal_app_headless_test OK\n";
  return 0;
}
