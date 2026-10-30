#include <condition_variable>
#include <cstdlib>
#include <deque>
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
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_push_button.h"
#include "apptraverse/ui/ui_push_button_presenter.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"
#include "apptraverse/ui/ui_presenters.h"

#include "minimal_app_controls.h"
#include "minimal_app_prepare.h"
#include "minimal_app_graph_headless.h"
#include "minimal_app_persistence.h"
#include "minimal_app_window_events.h"
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

ui::UiPushButtonPresenter& UiApplyButtonPresenter(MinimalApplication& application) {
  ui::UiDesktopWindow& window = UiMainWindow(application);
  auto presenter = ui::UiPushButtonPresenter::ptr::Declare(
      ae::CreateWith{*application.domain}.with_id(
          example::minimal::MinimalApplyButtonPresenterId()));
  presenter.Load();
  CHECK(presenter.is_valid());
  CHECK(presenter->button.id() == example::minimal::MinimalApplyButtonId());
  return *presenter;
}

ui::UiListContainerPresenter& UiRowListPresenter(MinimalApplication& application) {
  ui::UiDesktopWindow& window = UiMainWindow(application);
  (void)window;
  auto presenter = ui::UiListContainerPresenter::ptr::Declare(
      ae::CreateWith{*application.domain}.with_id(
          example::minimal::MinimalRowListPresenterId()));
  presenter.Load();
  CHECK(presenter.is_valid());
  CHECK(presenter->container.id() == example::minimal::MinimalRowListId());
  return *presenter;
}

example::minimal::MinimalAppList& ModelRowList(MinimalApplication& application) {
  application.row_list.Load();
  CHECK(application.row_list.is_valid());
  return *application.row_list;
}

void PumpModelWork(std::deque<ModelObjectProxy::ModelWork>& deferred,
                   ae::Domain& model_domain) {
  while (!deferred.empty()) {
    auto work = std::move(deferred.front());
    deferred.pop_front();
    work(model_domain);
  }
}

std::vector<std::uint8_t> SerializeStructuralRoot(Node const& model_root) {
  ByteSink structural;
  SerializeStructuralNodePublication(model_root, structural);
  return structural.bytes;
}

void PumpModelWorkAndApplyStructural(
    std::deque<ModelObjectProxy::ModelWork>& deferred,
    ae::Domain& model_domain, Node const& model_root,
    ApplicationUiSession& session) {
  PumpModelWork(deferred, model_domain);
  session.ApplyStructural(SerializeStructuralRoot(model_root));
}

ui::UiLocalizationString& WindowTitleMirror(MinimalApplication& ui_app) {
  ui::UiDesktopWindow& window = UiMainWindow(ui_app);
  window.presenter.Load();
  CHECK(window.presenter->title.is_valid());
  return *window.presenter->title;
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
      PrepareMinimalApplicationForRun(application);
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
    PrepareMinimalApplicationForRun(application);
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

void TestMinimalAppHeadlessUiCommandE2e() {
  EnsureObjectRegistration();
  EnsureHeadlessMinimalApplicationGraphRegistration();

  ae::RamDomainStorage model_storage;
  ae::Domain model_domain{model_storage};
  auto model_root = BuildHeadlessMinimalApplicationGraph(model_domain);

  std::deque<ModelObjectProxy::ModelWork> deferred;
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) {
    deferred.push_back(std::move(work));
  }};

  ByteSink initial;
  SerializeInitialPublication(*model_root, initial);
  ApplicationUiSession ui_session(initial.bytes, nullptr, proxy);

  MinimalApplication& ui_app =
      static_cast<MinimalApplication&>(ui_session.root());
  CHECK(WindowTitleMirror(ui_app).Resolve(0) == "Minimal App");
  ui_app.apply_button.Load();
  CHECK(ui_app.apply_button->enabled());
  MinimalApplication& model_app = *model_root;
  CHECK(ModelRowList(model_app).selection_index == -1);
  CHECK(ModelRowList(ui_app).selection_index == -1);

  UiRowListPresenter(ui_app).SubmitListRowSelectionChangedFromUi(1);
  PumpModelWorkAndApplyStructural(deferred, model_domain, model_app,
                                  ui_session);
  CHECK(ModelRowList(model_app).selection_index == 1);
  CHECK(ModelRowList(ui_app).selection_index == 1);

  UiApplyButtonPresenter(ui_app).SimulateClickForTest();
  PumpModelWorkAndApplyStructural(deferred, model_domain, model_app,
                                  ui_session);
  CHECK(WindowTitleMirror(ui_app).Resolve(0) == "Applied");
  CHECK(ModelRowList(ui_app).selection_index == -1);
  CHECK(!ui_app.apply_button->enabled());

  UiRowListPresenter(ui_app).SubmitListRowSelectionChangedFromUi(0);
  PumpModelWorkAndApplyStructural(deferred, model_domain, model_app,
                                  ui_session);
  CHECK(ModelRowList(ui_app).selection_index == 0);

  UiRowListPresenter(ui_app).SubmitListRowActivatedFromUi(0);
  PumpModelWorkAndApplyStructural(deferred, model_domain, model_app,
                                  ui_session);
  CHECK(WindowTitleMirror(ui_app).Resolve(0) == "Activated Row A");
}

ui::UiLocalizationString& AuxiliaryWindowTitleMirror(MinimalApplication& app) {
  app.auxiliary.window.Load();
  CHECK(app.auxiliary.window.is_valid());
  app.auxiliary.window->presenter.Load();
  CHECK(app.auxiliary.window->presenter.is_valid());
  auto& presenter = static_cast<ui::UiDesktopWindowPresenter&>(
      *app.auxiliary.window->presenter);
  CHECK(presenter.title.is_valid());
  return *presenter.title;
}

ui::UiListContainer& ModelAuxiliaryRowList(MinimalApplication& app) {
  app.auxiliary.row_list.Load();
  CHECK(app.auxiliary.row_list.is_valid());
  return *app.auxiliary.row_list;
}

ui::UiListContainer& UiAuxiliaryRowList(MinimalApplication& app) {
  app.auxiliary.row_list.Load();
  CHECK(app.auxiliary.row_list.is_valid());
  return *app.auxiliary.row_list;
}

ui::UiListContainerPresenter& UiAuxiliaryRowListPresenter(
    MinimalApplication& app) {
  app.auxiliary.presenters.row_list.Load();
  CHECK(app.auxiliary.presenters.row_list.is_valid());
  return *app.auxiliary.presenters.row_list;
}

ui::UiPushButtonPresenter& UiAuxiliaryApplyButtonPresenter(
    MinimalApplication& app) {
  app.auxiliary.presenters.apply_button.Load();
  CHECK(app.auxiliary.presenters.apply_button.is_valid());
  CHECK(app.auxiliary.presenters.apply_button->button.id() ==
        example::minimal::MinimalAuxiliaryApplyButtonId());
  return *app.auxiliary.presenters.apply_button;
}

ui::UiPushButtonPresenter& UiAuxiliaryCloserPresenter(
    MinimalApplication& app) {
  app.auxiliary.presenters.closer.Load();
  CHECK(app.auxiliary.presenters.closer.is_valid());
  return *app.auxiliary.presenters.closer;
}

ui::UiPushButtonPresenter& UiAuxiliaryOpenerPresenter(
    MinimalApplication& app) {
  app.presenters.auxiliary_opener.Load();
  CHECK(app.presenters.auxiliary_opener.is_valid());
  return *app.presenters.auxiliary_opener;
}

void TestMinimalAppAuxiliaryPersistColdLoad() {
  EnsureObjectRegistration();
  EnsureHeadlessMinimalApplicationGraphRegistration();

  std::filesystem::path const profile_dir =
      std::filesystem::temp_directory_path() / "minimal_app_aux_persist_test";
  std::error_code ec;
  std::filesystem::remove_all(profile_dir, ec);

  {
    DirectoryDomainStorage storage{profile_dir};
    ae::Domain domain{storage};
    auto application = BuildHeadlessMinimalApplicationGraph(domain);
    PrepareMinimalApplicationForRun(*application);
    application->OpenAuxiliaryWindowImpl();
    CHECK(application->auxiliary_presented_open);
    CHECK(PersistMinimalApplication(*application, storage) == 0);
  }

  {
    DirectoryDomainStorage storage{profile_dir};
    ae::Domain domain{storage};
    auto application =
        LoadApplication<MinimalApplication>(domain, MinimalApplicationRootId());
    CHECK(application->auxiliary_presented_open);
    PrepareMinimalApplicationForRun(*application);
    application->auxiliary.window.Load();
    CHECK(application->auxiliary.window.is_valid());
    CHECK(application->auxiliary.window.is_loaded());
  }
}

void TestMinimalAppAuxiliaryCloseOpenHeadless() {
  EnsureObjectRegistration();
  EnsureHeadlessMinimalApplicationGraphRegistration();
  example::minimal::EnsureMinimalAppWindowEventsRegistration();

  ae::RamDomainStorage model_storage;
  ae::Domain model_domain{model_storage};
  auto model_root = BuildHeadlessMinimalApplicationGraph(model_domain);

  std::deque<ModelObjectProxy::ModelWork> deferred;
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) {
    deferred.push_back(std::move(work));
  }};

  ByteSink initial;
  SerializeInitialPublication(*model_root, initial);
  ApplicationUiSession ui_session(initial.bytes, nullptr, proxy);

  MinimalApplication& ui_app =
      static_cast<MinimalApplication&>(ui_session.root());
  MinimalApplication& model_app = *model_root;
  CHECK(!model_app.auxiliary_presented_open);
  CHECK(!ui_app.auxiliary_presented_open);

  auto open = example::minimal::MinimalOpenAuxiliaryWindowEvent::ptr::Create(
      ae::CreateWith{model_domain});
  model_app.Commit(std::move(open));
  PumpModelWorkAndApplyStructural(deferred, model_domain, model_app,
                                  ui_session);
  CHECK(model_app.auxiliary_presented_open);
  CHECK(ui_app.auxiliary_presented_open);
  CHECK(AuxiliaryWindowTitleMirror(ui_app).Resolve(0) == "Detail Panel");

  UiAuxiliaryRowListPresenter(ui_app).SubmitListRowSelectionChangedFromUi(1);
  PumpModelWorkAndApplyStructural(deferred, model_domain, model_app,
                                  ui_session);
  CHECK(ModelAuxiliaryRowList(model_app).selection_index == 1);
  CHECK(UiAuxiliaryRowList(ui_app).selection_index == 1);
  CHECK(ModelRowList(model_app).selection_index == -1);

  UiApplyButtonPresenter(ui_app).SimulateClickForTest();
  PumpModelWorkAndApplyStructural(deferred, model_domain, model_app,
                                  ui_session);
  CHECK(WindowTitleMirror(ui_app).Resolve(0) == "Applied");
  CHECK(UiAuxiliaryRowList(ui_app).selection_index == 1);
  CHECK(AuxiliaryWindowTitleMirror(ui_app).Resolve(0) == "Detail Panel");

  UiAuxiliaryApplyButtonPresenter(ui_app).NotifyClickedFromUi();
  PumpModelWorkAndApplyStructural(deferred, model_domain, model_app,
                                  ui_session);
  CHECK(ModelAuxiliaryRowList(model_app).selection_index == -1);
  CHECK(UiAuxiliaryRowList(ui_app).selection_index == -1);
  CHECK(AuxiliaryWindowTitleMirror(ui_app).Resolve(0) == "Applied");
  ui_app.auxiliary.apply_button.Load();
  CHECK(ui_app.auxiliary.apply_button.is_valid());
  CHECK(!ui_app.auxiliary.apply_button->enabled());
  model_app.auxiliary.apply_button.Load();
  CHECK(model_app.auxiliary.apply_button.is_valid());
  CHECK(!model_app.auxiliary.apply_button->enabled());

  UiAuxiliaryCloserPresenter(ui_app).NotifyClickedFromUi();
  PumpModelWorkAndApplyStructural(deferred, model_domain, model_app,
                                  ui_session);
  CHECK(!model_app.auxiliary_presented_open);
  CHECK(!ui_app.auxiliary_presented_open);

  UiAuxiliaryOpenerPresenter(ui_app).NotifyClickedFromUi();
  PumpModelWorkAndApplyStructural(deferred, model_domain, model_app,
                                  ui_session);
  CHECK(model_app.auxiliary_presented_open);
  CHECK(ui_app.auxiliary_presented_open);
  CHECK(UiAuxiliaryRowList(ui_app).selection_index == -1);
  CHECK(AuxiliaryWindowTitleMirror(ui_app).Resolve(0) == "Applied");
  ui_app.auxiliary.apply_button.Load();
  CHECK(!ui_app.auxiliary.apply_button->enabled());
}

void TestMinimalAppOpenerLabelInInitialPublication() {
  EnsureObjectRegistration();
  EnsureHeadlessMinimalApplicationGraphRegistration();

  std::filesystem::path const profile_dir =
      std::filesystem::temp_directory_path() /
      "minimal_app_opener_label_publication_test";
  std::error_code ec;
  std::filesystem::remove_all(profile_dir, ec);

  example::minimal::BootstrapMinimalApplicationProfileIfAbsent(
      profile_dir, [](ae::Domain& domain) {
        return BuildHeadlessMinimalApplicationGraph(domain);
      });

  DirectoryDomainStorage model_storage{profile_dir};
  ae::Domain model_domain{model_storage};
  auto model_root =
      LoadApplication<MinimalApplication>(model_domain, MinimalApplicationRootId());
  PrepareMinimalApplicationForRun(*model_root);

  ByteSink initial;
  SerializeInitialPublication(*model_root, initial);

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  ByteSource in;
  in.data = initial.bytes.data();
  in.size = initial.bytes.size();
  ae::Ptr<ae::Obj> root = LoadInitialPublication(in, ui_domain, ui_storage);
  auto& app = static_cast<MinimalApplication&>(*root);

  app.apply_button.Load();
  app.apply_button->label.Load();
  CHECK(app.apply_button->label);

  app.auxiliary_opener.Load();
  CHECK(app.auxiliary_opener->label.is_valid());
  app.auxiliary_opener->label.Load();
  CHECK(app.auxiliary_opener->label);

  app.presenters.auxiliary_opener.Load();
  app.presenters.auxiliary_opener->button.Load();
  app.presenters.auxiliary_opener->button->label.Load();
  CHECK(app.presenters.auxiliary_opener->button->label);
  CHECK(app.presenters.auxiliary_opener->button->label->Resolve(0) ==
        "Open Detail");
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::test::TestMinimalAppOpenerLabelInInitialPublication();
  apptraverse::test::TestMinimalAppHeadlessSessionE2e();
  apptraverse::test::TestMinimalAppHeadlessUiCommandE2e();
  apptraverse::test::TestMinimalAppAuxiliaryPersistColdLoad();
  apptraverse::test::TestMinimalAppAuxiliaryCloseOpenHeadless();
  std::cout << "minimal_app_headless_test OK\n";
  return 0;
}
