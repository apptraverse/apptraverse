#include <atomic>
#include <cstdlib>
#include <future>
#include <iostream>
#include <thread>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/event_for.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/node_for.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/windows/win_application_host.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_presented_window_composition.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_string.h"

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

wchar_t const kHostTestLoadingClass[] = L"AppTraverseTestWinApplicationHost";
wchar_t const kHostTestLoadingTitle[] = L"Host test loading";
wchar_t const kHostTestLoadingMessage[] = L"Host test loading";

ae::ObjId const kRootId{0xC6C60001u};
ae::ObjId const kWindowId{0xC6C60002u};
ae::ObjId const kPresenterId{0xC6C60003u};

int g_host_presenter_unload_total{0};

class HostBumpEvent;

class HostWindow : public NodeFor<HostWindow, ui::UiDesktopWindow> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::HostWindow", HostWindow,
                           ui::UiDesktopWindow, 0)

 protected:
  HostWindow() = default;

 public:
  explicit HostWindow(ae::ObjProp prop) : NodeFor{prop} {}
};

class HostTestRoot : public NodeFor<HostTestRoot> {
  APPTRAVERSE_OBJECT(HostTestRoot, Node, 0)

 protected:
  HostTestRoot() = default;

 public:
  explicit HostTestRoot(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value), AE_MMBR(presented_window))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, value, presented_window);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, value, presented_window);
  }

  std::int32_t value{0};
  HostWindow::ptr presented_window;

  void Apply(HostBumpEvent const& event);
};

class HostBumpEvent : public EventFor<HostTestRoot, HostBumpEvent> {
  APPTRAVERSE_OBJECT(HostBumpEvent, Event, 0)

 protected:
  HostBumpEvent() = default;

 public:
  explicit HostBumpEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(delta))

  std::int32_t delta{0};
};

class HostHeadlessPresenter : public ui::UiDesktopWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::HostHeadlessPresenter",
                           HostHeadlessPresenter,
                           ui::UiDesktopWindowPresenter, 0)

 protected:
  HostHeadlessPresenter() = default;

 public:
  explicit HostHeadlessPresenter(ae::ObjProp prop)
      : ui::UiDesktopWindowPresenter{prop} {}

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_);
  }

  void OnUnload() override { ++g_host_presenter_unload_total; }
};

APPTRAVERSE_REGISTER(HostTestRoot);
APPTRAVERSE_REGISTER(HostBumpEvent);
APPTRAVERSE_REGISTER(HostHeadlessPresenter);
APPTRAVERSE_REGISTER(HostWindow);

void HostTestRoot::Apply(HostBumpEvent const& event) {
  value += event.delta;
  NoteMaterializedChange();
}

void CommitBump(HostTestRoot& root, ae::Domain& domain, std::int32_t delta) {
  auto ev = HostBumpEvent::ptr::Create(ae::CreateWith{domain});
  ev->delta = delta;
  root.Commit(std::move(ev));
}

void BuildModelGraph(ae::Domain& domain, HostTestRoot::ptr& root) {
  root = HostTestRoot::ptr::Create(ae::CreateWith{domain}.with_id(kRootId));
  InitializeRuntimeNode(*root);

  auto locale = ui::UiLocaleSettings::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*locale, *root);
  auto en = ui::UiString::ptr::Create(ae::CreateWith{domain});
  en->utf8 = "EN";
  auto localized = ui::UiLocalizationString::ptr::Create(
      ae::CreateWith{domain});
  localized->strings = {ui::UiString::ptr::MakeFromThis(&*en)};
  InitializeRuntimeNode(*localized, *root);

  root->presented_window = HostWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(kWindowId));
  auto presenter = HostHeadlessPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(kPresenterId));
  ui::ComposePresentedWindow(root->presented_window, presenter, localized,
                             locale);
  InitializeRuntimeNode(*root->presented_window, *root);
}

void TestHostPublicationLifecycle() {
  g_host_presenter_unload_total = 0;
  EnsureObjectRegistration();
  ui::EnsureUiPresenterRegistration();

  ae::RamDomainStorage model_storage;
  ae::Domain model_domain{model_storage};
  HostTestRoot::ptr model_root;
  BuildModelGraph(model_domain, model_root);
  CommitBump(*model_root, model_domain, 4);

  ApplicationModelRuntime runtime;

  std::promise<void> initial_published;
  std::promise<void> incremental_published;
  std::future<void> initial_future = initial_published.get_future();
  std::future<void> incremental_future = incremental_published.get_future();

  std::atomic<bool> sidecar_ran{false};
  std::atomic<bool> after_incremental_ran{false};
  std::atomic<std::int32_t> ui_value{-1};
  ae::Domain* ui_domain_ptr{nullptr};

  windows::WinApplicationHostCallbacks callbacks;
  callbacks.run_model = [&](ApplicationPublishedCallback on_published) {
    runtime.Run(model_domain, *model_root, [&](ApplicationPublicationKind kind) {
      on_published(kind);
      if (kind == ApplicationPublicationKind::Initial) {
        initial_published.set_value();
      } else {
        incremental_published.set_value();
      }
    });
  };
  callbacks.request_stop = [&] { runtime.RequestStop(); };
  callbacks.on_desktop_window_close = [](ae::Obj&, ae::Obj&,
                                         ModelObjectProxy&) {};
  callbacks.take_incremental_sidecar = [&] { sidecar_ran = true; };
  callbacks.after_incremental = [&](ae::Obj& application_root) {
    CHECK(FindWindowW(kHostTestLoadingClass, nullptr) == nullptr);
    after_incremental_ran = true;
    CHECK(sidecar_ran.load());
    auto& ui_root = static_cast<HostTestRoot&>(application_root);
    ui_domain_ptr = ui_root.domain;
    ui_value = ui_root.value;
    runtime.RequestStop();
  };

  windows::WinApplicationLoadingOptions loading{
      kHostTestLoadingClass, kHostTestLoadingTitle, kHostTestLoadingMessage, 80,
      80, 260, 90};

  windows::WinApplicationHost host(runtime, loading, std::move(callbacks));
  CHECK(FindWindowW(kHostTestLoadingClass, nullptr) != nullptr);

  std::thread driver([&] {
    initial_future.wait();
    runtime.Post([&](ae::Domain& domain) {
      CommitBump(*model_root, domain, 6);
    });
    incremental_future.wait();
  });

  host.Run();
  driver.join();

  CHECK(ui_value.load() == 10);
  CHECK(ui_domain_ptr != nullptr);
  CHECK(ui_domain_ptr != &model_domain);
  CHECK(after_incremental_ran.load());
  CHECK(g_host_presenter_unload_total == 1);
}

void TestHostDestructorReleasesLoadingClass() {
  {
    ApplicationModelRuntime runtime;
    ae::RamDomainStorage model_storage;
    ae::Domain model_domain{model_storage};
    HostTestRoot::ptr model_root;
    EnsureObjectRegistration();
    ui::EnsureUiPresenterRegistration();
    BuildModelGraph(model_domain, model_root);

    std::promise<void> initial_published;
    std::future<void> initial_future = initial_published.get_future();

    windows::WinApplicationHostCallbacks callbacks;
    callbacks.run_model = [&](ApplicationPublishedCallback on_published) {
      runtime.Run(model_domain, *model_root, [&](ApplicationPublicationKind kind) {
        on_published(kind);
        if (kind == ApplicationPublicationKind::Initial) {
          initial_published.set_value();
        }
        runtime.RequestStop();
      });
    };
    callbacks.request_stop = [&] { runtime.RequestStop(); };
    callbacks.on_desktop_window_close = [](ae::Obj&, ae::Obj&,
                                           ModelObjectProxy&) {};

    windows::WinApplicationLoadingOptions loading{
        kHostTestLoadingClass, kHostTestLoadingTitle, kHostTestLoadingMessage,
        80, 80, 260, 90};

    windows::WinApplicationHost host(runtime, loading, std::move(callbacks));
    std::thread driver([&] { initial_future.wait(); });
    host.Run();
    driver.join();
  }

  HINSTANCE const instance = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = instance;
  wc.lpszClassName = kHostTestLoadingClass;
  CHECK(RegisterClassW(&wc) != 0);
  CHECK(UnregisterClassW(kHostTestLoadingClass, instance) != 0);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::test::TestHostPublicationLifecycle();
  apptraverse::test::TestHostDestructorReleasesLoadingClass();
  std::cout << "win_application_host_test OK\n";
  return 0;
}
