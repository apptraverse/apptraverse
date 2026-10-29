#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/application_model_session.h"
#include "apptraverse/application_ui_session.h"
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

constexpr std::int32_t kInitialX = 120;
constexpr std::int32_t kInitialY = 80;
constexpr std::int32_t kInitialW = 640;
constexpr std::int32_t kInitialH = 480;
constexpr std::int32_t kReportedX = 33;
constexpr std::int32_t kReportedY = 44;
constexpr std::int32_t kReportedW = 700;
constexpr std::int32_t kReportedH = 500;

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
  std::vector<std::uint8_t> bytes;
};

void WaitPublication(PublicationWaiter& waiter) {
  std::unique_lock<std::mutex> lock{waiter.mu};
  waiter.cv.wait(lock, [&] { return waiter.pending > 0; });
  --waiter.pending;
}

void TestMinimalAppHeadlessSessionE2e() {
  EnsureObjectRegistration();
  EnsureHeadlessMinimalApplicationGraphRegistration();

  ae::RamDomainStorage model_storage;
  {
    ae::Domain bootstrap_domain{model_storage};
    auto application = BuildHeadlessMinimalApplicationGraph(bootstrap_domain);
    FinalizeDistilledGraph(*application);
    SaveDistilledRoot(*application);
  }

  ApplicationModelRuntime runtime;
  PublicationWaiter waiter;
  std::unique_ptr<ApplicationUiSession> ui_session;
  int session_result = 1;
  std::size_t native_frame_events_at_incremental = 0;

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
  hooks.persist = [](MinimalApplication& application) -> int {
    ui::SavePresentedWindow(application.main_window);
    MinimalApplication::ptr::MakeFromThis(&application).Save();
    return 0;
  };

  std::thread model_thread{[&] {
    session_result = RunApplicationModelSession(
        model_storage, MinimalApplicationRootId(), runtime,
        [&](ApplicationPublicationKind kind) {
          std::vector<std::uint8_t> bytes = runtime.TakePublishedCopy();
          runtime.NotifyPublicationConsumed();
          {
            std::lock_guard<std::mutex> lock{waiter.mu};
            waiter.kind = kind;
            waiter.bytes = std::move(bytes);
            ++waiter.pending;
          }
          waiter.cv.notify_one();
        },
        hooks);
  }};

  WaitPublication(waiter);
  CHECK(waiter.kind == ApplicationPublicationKind::Initial);
  ui_session = std::make_unique<ApplicationUiSession>(waiter.bytes, nullptr,
                                                      model_proxy);
  MinimalApplication& ui_app =
      static_cast<MinimalApplication&>(ui_session->root());
  ui::UiDesktopWindow& ui_window = UiMainWindow(ui_app);
  CHECK(ui_window.x() == kInitialX);
  CHECK(ui_window.y() == kInitialY);
  CHECK(ui_window.client_width() == kInitialW);
  CHECK(ui_window.client_height() == kInitialH);

  UiMainPresenter(ui_app).SubmitFrameFromNative(kReportedX, kReportedY,
                                                kReportedW, kReportedH);

  WaitPublication(waiter);
  CHECK(waiter.kind == ApplicationPublicationKind::Incremental);
  CHECK(ui_window.x() == kInitialX);
  CHECK(ui_window.y() == kInitialY);
  CHECK(ui_window.client_width() == kInitialW);
  CHECK(ui_window.client_height() == kInitialH);

  ui_session->ApplyStructural(waiter.bytes);
  CHECK(ui_window.x() == kReportedX);
  CHECK(ui_window.y() == kReportedY);
  CHECK(ui_window.client_width() == kReportedW);
  CHECK(ui_window.client_height() == kReportedH);

  runtime.RequestStop();
  model_thread.join();
  ui_session.reset();

  CHECK(session_result == 0);
  CHECK(native_frame_events_at_incremental == 1);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::test::TestMinimalAppHeadlessSessionE2e();
  std::cout << "minimal_app_headless_test OK\n";
  return 0;
}
